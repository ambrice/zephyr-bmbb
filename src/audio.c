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

static bool prepare_transfer(const struct device *i2s_dev)
{
	int ret;

	for (int i = 0; i < INITIAL_BLOCKS; ++i) {
		void *mem_block;

		ret = k_mem_slab_alloc(&mem_slab, &mem_block, K_NO_WAIT);
		if (ret < 0) {
			printk("Failed to allocate audio TX block %d: %d\n", i, ret);
			return false;
		}

		memset(mem_block, 0, BLOCK_SIZE);

		ret = i2s_write(i2s_dev, mem_block, BLOCK_SIZE);
		if (ret < 0) {
			printk("Failed to write block %d: %d\n", i, ret);
			return false;
		}
	}

	return true;
}

int audio_init(void)
{
	const struct device *const i2s_dev = DEVICE_DT_GET(I2S_NODE);

	if (!device_is_ready(i2s_dev)) {
		LOG_ERR("%s is not ready\n", i2s_dev->name);
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

	int ret = i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
	if (ret < 0) {
		LOG_ERR("Failed to configure audio stream: %d\n", ret);
	}
	return ret;
}

/* TODO: Do this in a work queue */
int audio_play(const char *filename)
{
	struct fs_file_t file;

	fs_file_t_init(&file);
	int err = fs_open(&file, filename, FS_O_READ);
	if (err != 0) {
		LOG_ERR("Failed to open %s for reading", filename);
		return err;
	}

	struct wav_header wavh;
	size_t len = fs_read(&file, &wavh, sizeof(wavh));
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

	fs_close(&file);
	return 0;
}
