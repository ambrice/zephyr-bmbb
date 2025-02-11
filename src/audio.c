#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/logging/log.h>

#include "audio.h"

#define SAMPLE_FREQUENCY    44100
#define SAMPLE_BIT_WIDTH    16
#define BYTES_PER_SAMPLE    sizeof(int16_t)
#define NUMBER_OF_CHANNELS  1
/* Such block length provides delay of 100 ms. */
#define SAMPLES_PER_BLOCK   ((SAMPLE_FREQUENCY / 10) * NUMBER_OF_CHANNELS)
#define INITIAL_BLOCKS      2
#define TIMEOUT             1000


#define BLOCK_SIZE  (BYTES_PER_SAMPLE * SAMPLES_PER_BLOCK)
#define BLOCK_COUNT (INITIAL_BLOCKS + 2)
K_MEM_SLAB_DEFINE_STATIC(mem_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

#define I2S_NODE  DT_NODELABEL(i2s0)

LOG_MODULE_DECLARE(bmbb);

/* Define the thread to play audio */
#define AUDIO_STACK_SIZE 2048
K_THREAD_STACK_DEFINE(audio_stack_area, AUDIO_STACK_SIZE);
struct k_thread audio_thread_data;

static struct {
	const struct device *i2s_dev;
	k_tid_t tid;
	bool cancel;
	void *mem_block[BLOCK_COUNT];
	struct fs_file_t file;
} s_ctx;

void handle_playback(void *, void *, void *)
{
	int ret;
	size_t len;

	/* Apparently need to pre-fill the i2s before starting it */
	len = fs_read(&s_ctx.file, s_ctx.mem_block[0], BLOCK_SIZE);
	if (len < 0) {
		LOG_ERR("Failed to read from wav file: %d", len);
		goto done;
	} else if (len > 0) {
		/* Write the block to the I2S (blocking) */
		ret = i2s_write(s_ctx.i2s_dev, s_ctx.mem_block[0], len);
		if (ret < 0) {
			LOG_ERR("Failed to write wav block: %d", ret);
			goto done;
		}
	}
	k_yield();

	/* Start the stream */
	ret = i2s_trigger(s_ctx.i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret < 0) {
		LOG_ERR("Failed to start i2s stream: %d", ret);
		goto done;
	}

	/* Fill with the rest */
	int count=0;
	int block_idx = 1;
	do {
		k_yield();
		if (s_ctx.cancel) {
			LOG_INF("audio transmit cancelled");
			break;
		}

		/* Read a block from the file */
		len = fs_read(&s_ctx.file, s_ctx.mem_block[block_idx], BLOCK_SIZE);
		if (len < 0) {
			LOG_ERR("Failed to read from wav file: %d", len);
			break;
		} else if (len > 0) {
			/* Write the block to the I2S (blocking) */
			ret = i2s_write(s_ctx.i2s_dev, s_ctx.mem_block[block_idx], len);
			if (ret < 0) {
				LOG_ERR("Failed to write wav block %d of len %d: %d", block_idx, len, ret);
				break;
			}
		}
		if (len < BLOCK_SIZE) {
			/* End of file */
			break;
		}
		LOG_INF("Sent block %d", count++);
		block_idx = (block_idx + 1) % BLOCK_COUNT;
	} while(1);

done:
	i2s_trigger(s_ctx.i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
	fs_close(&s_ctx.file);
}

int audio_init(void)
{
	s_ctx.i2s_dev = DEVICE_DT_GET(I2S_NODE);
	s_ctx.tid = NULL;
	s_ctx.cancel = false;
	fs_file_t_init(&s_ctx.file);

	if (!device_is_ready(s_ctx.i2s_dev)) {
		LOG_ERR("%s is not ready", s_ctx.i2s_dev->name);
		return 0;
	}

	struct i2s_config cfg = {
		.word_size = SAMPLE_BIT_WIDTH,
		.channels = NUMBER_OF_CHANNELS,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
		.frame_clk_freq = SAMPLE_FREQUENCY,
		.mem_slab = &mem_slab,
		.block_size = BLOCK_SIZE,
		.timeout = TIMEOUT,
	};

	int ret = i2s_configure(s_ctx.i2s_dev, I2S_DIR_TX, &cfg);
	if (ret < 0) {
		LOG_ERR("Failed to configure audio stream: %d", ret);
	}

	/* Allocate the mem_slab blocks */
	for (int i = 0; i < BLOCK_COUNT; ++i) {
		ret = k_mem_slab_alloc(&mem_slab, &s_ctx.mem_block[i], K_NO_WAIT);
		if (ret < 0) {
			LOG_ERR("Failed to allocate audio TX block %d: %d", i, ret);
			return ret;
		}
	}

	return ret;
}

void audio_cancel(void)
{
	if (s_ctx.tid != NULL) {
		s_ctx.cancel = true;
		k_thread_join(s_ctx.tid, K_FOREVER);
	}
}

bool audio_busy(void)
{
	if (s_ctx.tid != NULL && k_thread_join(s_ctx.tid, K_NO_WAIT) != 0) {
		return true;
	}
	return false;
}

int audio_play(const char *filename)
{
	/* Make sure we're not currently playing */
	if (audio_busy()) {
		LOG_ERR("audio_play called while audio thread is running");
		return -EBUSY;
	}

	int err = fs_open(&s_ctx.file, filename, FS_O_READ);
	if (err != 0) {
		LOG_ERR("Failed to open %s for reading", filename);
		return err;
	}

	struct wav_header wavh;
	size_t len = fs_read(&s_ctx.file, &wavh, sizeof(wavh));
	if (len < sizeof(wavh)) {
		LOG_ERR("Only read %d bytes from audio file %s", len, filename);
		return len;
	}

	LOG_INF("Wav file data:");
	LOG_INF("\tfile_type_block_id=0x%08x", wavh.file_type_bloc_id);
	LOG_INF("\tfile_size=%d", wavh.file_size);
	LOG_INF("\tfile_format_id=0x%08x", wavh.file_format_id);
	LOG_INF("\tformat_bloc_id=0x%08x", wavh.format_bloc_id);
	LOG_INF("\tformat_bloc_size=%d", wavh.format_bloc_size);
	LOG_INF("\taudio_format=%d", wavh.audio_format);
	LOG_INF("\tnbr_channels=%d", wavh.nbr_channels);
	LOG_INF("\tfrequency=%d", wavh.frequency);
	LOG_INF("\tbytes_per_sec=%d", wavh.bytes_per_sec);
	LOG_INF("\tbytes_per_bloc=%d", wavh.bytes_per_bloc);
	LOG_INF("\tbits_per_sample=%d", wavh.bits_per_sample);
	LOG_INF("\tdata_block_id=0x%08x", wavh.data_block_id);
	LOG_INF("\tdata_size=%d", wavh.data_size);

	if (wavh.audio_format != 1 || wavh.nbr_channels != 1 || wavh.frequency != 44100 || wavh.bits_per_sample != 16) {
		LOG_ERR("WAV file format incorrect, must be mono PCM, 16 bits per sample, 44100");
		fs_close(&s_ctx.file);
		return -EINVAL;
	}

	s_ctx.cancel = false;
	s_ctx.tid = k_thread_create(&audio_thread_data, audio_stack_area,
			K_THREAD_STACK_SIZEOF(audio_stack_area),
			handle_playback, NULL, NULL, NULL, 5, 0, K_NO_WAIT);

	return 0;
}
