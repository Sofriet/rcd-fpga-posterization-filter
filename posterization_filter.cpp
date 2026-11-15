#include <stdint.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

#define WIDTH 1280
#define HEIGHT 720

typedef ap_axiu<32, 1, 1, 1> pixel_data;
typedef hls::stream<pixel_data> pixel_stream;


static inline uint8_t rgb2gray(uint32_t rgb)
{
#pragma HLS INLINE
	uint8_t r = (uint8_t)(rgb & 0xFF);
	uint8_t g = (uint8_t)((rgb >> 8) & 0xFF);
	uint8_t b = (uint8_t)((rgb >> 16) & 0xFF);
	return (uint8_t)(((uint16_t)(77 * r) + (uint16_t)(150 * g) + (uint16_t)(29 * b)) >> 8);
}

static inline uint8_t gaussian_filter_3x3(uint8_t w[3][3])
{
#pragma HLS INLINE
	int s = w[0][0] + 2*w[0][1] + w[0][2] + 2*w[1][0] + 4*w[1][1] + 2*w[1][2] + w[2][0] + 2*w[2][1] + w[2][2];
	return (uint8_t)((s + 8) >> 4);
}

static inline uint8_t quantize(uint8_t c, uint8_t levels)
{
#pragma HLS INLINE
	// if (levels <= 1)
		// return c;
	static const uint8_t StepLUT[17] = {1, 1, 128, 85, 64, 51, 42, 36, 32, 28, 25, 23, 21, 19, 18, 17, 16};
	static const uint16_t InvStepLUT[17] = {0, 0, 512, 771, 1024, 1285, 1560, 1820, 2048, 2340, 2621, 2858, 3120, 3449, 3640, 3855, 4096};
	uint8_t lvl = levels;
	// if (lvl > 16)
		// lvl = 16;
	uint8_t step = StepLUT[lvl];
	uint16_t inv = InvStepLUT[lvl];

	uint16_t q = (uint16_t)(((uint32_t)c * (uint32_t)inv) >> 16);
	uint16_t prod = (uint16_t)(q * step);
	if (prod > c)
	{
		q--;
	}
	else if ((uint16_t)(prod + step) <= c)
	{
		q++;
	}

	return (uint8_t)(q * step);
}

// Clamp to [0,255]
static inline uint8_t clamp8(int v)
{
#pragma HLS INLINE
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return (uint8_t)v;
}

void cartoon_filter(pixel_stream &src,
					pixel_stream &dst,
					uint8_t edge_threshold,
					uint16_t bright_factor,
					uint8_t quant_levels)
{
#pragma HLS INTERFACE ap_ctrl_none port=return
#pragma HLS INTERFACE axis port=&src
#pragma HLS INTERFACE axis port=&dst
#pragma HLS INTERFACE s_axilite port=edge_threshold
#pragma HLS INTERFACE s_axilite port=bright_factor
#pragma HLS INTERFACE s_axilite port=quant_levels
#pragma HLS PIPELINE II=1

	static uint16_t x = 0;
	static uint16_t y = 0;

	// Line buffer
	static uint8_t gray_line_buffer_0[WIDTH];
	static uint8_t gray_line_buffer_1[WIDTH];
	static uint32_t rgb_line_buffer_0[WIDTH];
	static uint32_t rgb_line_buffer_1[WIDTH];
	static uint32_t rgb_line_buffer_2[WIDTH];
	static uint8_t gray_denoised_line_buffer_0[WIDTH];
	static uint8_t gray_denoised_line_buffer_1[WIDTH];
#pragma HLS BIND_STORAGE variable=gray_line_buffer_0 type=ram_t2p impl=bram
#pragma HLS BIND_STORAGE variable=gray_line_buffer_1 type=ram_t2p impl=bram
#pragma HLS BIND_STORAGE variable=rgb_line_buffer_0 type=ram_t2p impl=bram
#pragma HLS BIND_STORAGE variable=rgb_line_buffer_1 type=ram_t2p impl=bram
#pragma HLS BIND_STORAGE variable=rgb_line_buffer_2 type=ram_t2p impl=bram
#pragma HLS BIND_STORAGE variable=gray_denoised_line_buffer_0 type=ram_t2p impl=bram
#pragma HLS BIND_STORAGE variable=gray_denoised_line_buffer_1 type=ram_t2p impl=bram

	static uint8_t gray_win[3][3];
	// static uint32_t rgb_win[3][3];
	static uint32_t rgb_delayed_win[3][3];
	static uint8_t gray_denoised_win[3][3];
#pragma HLS ARRAY_PARTITION variable=gray_win complete dim=0
// #pragma HLS ARRAY_PARTITION variable=rgb_win complete dim=0
#pragma HLS ARRAY_PARTITION variable=rgb_delayed_win complete dim=0
#pragma HLS ARRAY_PARTITION variable=gray_denoised_win complete dim=0

	// static bool prev_edge = false;

	pixel_data pixel_in;
	src >> pixel_in;

	if (pixel_in.user)
	{
		x = 0;
		y = 0;
		// prev_edge = false;
	}

	pixel_data pixel_out = pixel_in;

	uint32_t rgb_in = (pixel_in.data & 0x00FFFFFF);
	uint8_t gray_in = rgb2gray(rgb_in);
	uint8_t gray_denoised = gray_in;

	gray_win[0][0] = gray_win[0][1];
	gray_win[0][1] = gray_win[0][2];
	gray_win[0][2] = gray_line_buffer_1[x];
	gray_win[1][0] = gray_win[1][1];
	gray_win[1][1] = gray_win[1][2];
	gray_win[1][2] = gray_line_buffer_0[x];
	gray_win[2][0] = gray_win[2][1];
	gray_win[2][1] = gray_win[2][2];
	gray_win[2][2] = gray_in;

	/*
	rgb_win[0][0] = rgb_win[0][1];
	rgb_win[0][1] = rgb_win[0][2];
	rgb_win[0][2] = rgb_line_buffer_1[x];
	rgb_win[1][0] = rgb_win[1][1];
	rgb_win[1][1] = rgb_win[1][2];
	rgb_win[1][2] = rgb_line_buffer_0[x];
	rgb_win[2][0] = rgb_win[2][1];
	rgb_win[2][1] = rgb_win[2][2];
	rgb_win[2][2] = rgb_in;
	*/

	rgb_delayed_win[0][0] = rgb_delayed_win[0][1];
	rgb_delayed_win[0][1] = rgb_delayed_win[0][2];
	rgb_delayed_win[0][2] = rgb_line_buffer_2[x];
	rgb_delayed_win[1][0] = rgb_delayed_win[1][1];
	rgb_delayed_win[1][1] = rgb_delayed_win[1][2];
	rgb_delayed_win[1][2] = rgb_line_buffer_1[x];
	rgb_delayed_win[2][0] = rgb_delayed_win[2][1];
	rgb_delayed_win[2][1] = rgb_delayed_win[2][2];
	rgb_delayed_win[2][2] = rgb_line_buffer_0[x];


	gray_line_buffer_1[x] = gray_line_buffer_0[x];
	gray_line_buffer_0[x] = gray_in;

	rgb_line_buffer_2[x] = rgb_line_buffer_1[x];
	rgb_line_buffer_1[x] = rgb_line_buffer_0[x];
	rgb_line_buffer_0[x] = rgb_in;

	uint32_t rgb_out = rgb_in;

	if (x > 1 && y > 1)
	{
		gray_denoised = gaussian_filter_3x3(gray_win);
	}

	gray_denoised_win[0][0] = gray_denoised_win[0][1];
	gray_denoised_win[0][1] = gray_denoised_win[0][2];
	gray_denoised_win[0][2] = gray_denoised_line_buffer_0[x];
	gray_denoised_win[1][0] = gray_denoised_win[1][1];
	gray_denoised_win[1][1] = gray_denoised_win[1][2];
	gray_denoised_win[1][2] = gray_denoised_line_buffer_1[x];
	gray_denoised_win[2][0] = gray_denoised_win[2][1];
	gray_denoised_win[2][1] = gray_denoised_win[2][2];
	gray_denoised_win[2][2] = gray_denoised;

	gray_denoised_line_buffer_1[x] = gray_denoised_line_buffer_0[x];
	gray_denoised_line_buffer_0[x] = gray_denoised;

	if (x > 1 && y > 1)
	{
		int gx = - gray_denoised_win[0][0] + gray_denoised_win[0][2] - 2*gray_denoised_win[1][0] +
				 2*gray_denoised_win[1][2] - gray_denoised_win[2][0] + gray_denoised_win[2][2];
		int gy = - gray_denoised_win[0][0] - 2*gray_denoised_win[0][1] - gray_denoised_win[0][2] +
				 gray_denoised_win[2][0] + 2*gray_denoised_win[2][1] + gray_denoised_win[2][2];

		if (gx < 0)
		{
			gx = -gx;
		}
		if (gy < 0)
		{
			gy = -gy;
		}

		int mag = gx + gy; // mag = abs(gx) + abs(gy); an approximation of mag = sqrt(gx*gx + gy*gy)
		bool edge = (mag > edge_threshold);

		int r_sum = 0;
		int g_sum = 0;
		int b_sum = 0;

		/*
		r_sum = (uint8_t)(rgb_win[0][0] & 0xFF) + (uint8_t)(rgb_win[0][1] & 0xFF) + (uint8_t)(rgb_win[0][2] & 0xFF)
			  + (uint8_t)(rgb_win[1][0] & 0xFF) + (uint8_t)(rgb_win[1][1] & 0xFF) + (uint8_t)(rgb_win[1][2] & 0xFF)
			  + (uint8_t)(rgb_win[2][0] & 0xFF) + (uint8_t)(rgb_win[2][1] & 0xFF) + (uint8_t)(rgb_win[2][2] & 0xFF);
		g_sum = (uint8_t)((rgb_win[0][0] >> 8) & 0xFF) + (uint8_t)((rgb_win[0][1] >> 8) & 0xFF) + (uint8_t)((rgb_win[0][2] >> 8) & 0xFF)
			  + (uint8_t)((rgb_win[1][0] >> 8) & 0xFF) + (uint8_t)((rgb_win[1][1] >> 8) & 0xFF) + (uint8_t)((rgb_win[1][2] >> 8) & 0xFF)
		      + (uint8_t)((rgb_win[2][0] >> 8) & 0xFF) + (uint8_t)((rgb_win[2][1] >> 8) & 0xFF) + (uint8_t)((rgb_win[2][2] >> 8) & 0xFF);
		b_sum = (uint8_t)((rgb_win[0][0] >> 16) & 0xFF) + (uint8_t)((rgb_win[0][1] >> 16) & 0xFF) + (uint8_t)((rgb_win[0][2] >> 16) & 0xFF)
			  + (uint8_t)((rgb_win[1][0] >> 16) & 0xFF) + (uint8_t)((rgb_win[1][1] >> 16) & 0xFF) + (uint8_t)((rgb_win[1][2] >> 16) & 0xFF)
			  + (uint8_t)((rgb_win[2][0] >> 16) & 0xFF) + (uint8_t)((rgb_win[2][1] >> 16) & 0xFF) + (uint8_t)((rgb_win[2][2] >> 16) & 0xFF);
		*/

		r_sum = (uint8_t)(rgb_delayed_win[0][0] & 0xFF) + (uint8_t)(rgb_delayed_win[0][1] & 0xFF) + (uint8_t)(rgb_delayed_win[0][2] & 0xFF)
			  + (uint8_t)(rgb_delayed_win[1][0] & 0xFF) + (uint8_t)(rgb_delayed_win[1][1] & 0xFF) + (uint8_t)(rgb_delayed_win[1][2] & 0xFF)
			  + (uint8_t)(rgb_delayed_win[2][0] & 0xFF) + (uint8_t)(rgb_delayed_win[2][1] & 0xFF) + (uint8_t)(rgb_delayed_win[2][2] & 0xFF);
		g_sum = (uint8_t)((rgb_delayed_win[0][0] >> 8) & 0xFF) + (uint8_t)((rgb_delayed_win[0][1] >> 8) & 0xFF) + (uint8_t)((rgb_delayed_win[0][2] >> 8) & 0xFF)
			  + (uint8_t)((rgb_delayed_win[1][0] >> 8) & 0xFF) + (uint8_t)((rgb_delayed_win[1][1] >> 8) & 0xFF) + (uint8_t)((rgb_delayed_win[1][2] >> 8) & 0xFF)
			  + (uint8_t)((rgb_delayed_win[2][0] >> 8) & 0xFF) + (uint8_t)((rgb_delayed_win[2][1] >> 8) & 0xFF) + (uint8_t)((rgb_delayed_win[2][2] >> 8) & 0xFF);
		b_sum = (uint8_t)((rgb_delayed_win[0][0] >> 16) & 0xFF) + (uint8_t)((rgb_delayed_win[0][1] >> 16) & 0xFF) + (uint8_t)((rgb_delayed_win[0][2] >> 16) & 0xFF)
			  + (uint8_t)((rgb_delayed_win[1][0] >> 16) & 0xFF) + (uint8_t)((rgb_delayed_win[1][1] >> 16) & 0xFF) + (uint8_t)((rgb_delayed_win[1][2] >> 16) & 0xFF)
			  + (uint8_t)((rgb_delayed_win[2][0] >> 16) & 0xFF) + (uint8_t)((rgb_delayed_win[2][1] >> 16) & 0xFF) + (uint8_t)((rgb_delayed_win[2][2] >> 16) & 0xFF);

		int r_avg = r_sum / 9;
		int g_avg = g_sum / 9;
		int b_avg = b_sum / 9;

		uint8_t r_quantized = quantize(clamp8((r_avg * bright_factor) >> 8), quant_levels);
		uint8_t g_quantized = quantize(clamp8((g_avg * bright_factor) >> 8), quant_levels);
		uint8_t b_quantized = quantize(clamp8((b_avg * bright_factor) >> 8), quant_levels);

		uint32_t rgb_cartoonized = (uint32_t)(r_quantized & 0xFF) | (uint32_t)((g_quantized & 0xFF) << 8) | (uint32_t)((b_quantized & 0xFF) << 16);

		if (edge)
		{
			rgb_out = 0x00000000;
		}
		else
		{
			rgb_out = rgb_cartoonized;
		}
	}
	else
	{
		uint8_t r = (rgb_in & 0xFF);
		uint8_t g = ((rgb_in >> 8) & 0xFF);
		uint8_t b = ((rgb_in >> 16) & 0xFF);

		uint8_t r_quantized = quantize(clamp8((r * bright_factor) >> 8), quant_levels);
		uint8_t g_quantized = quantize(clamp8((g * bright_factor) >> 8), quant_levels);
		uint8_t b_quantized = quantize(clamp8((b * bright_factor) >> 8), quant_levels);

		rgb_out = (uint32_t)(r_quantized & 0xFF) | (uint32_t)((g_quantized & 0xFF) << 8) | (uint32_t)((b_quantized & 0xFF) << 16);
	}

	uint32_t alpha = pixel_in.data & 0xFF000000;

	pixel_out.data = alpha | (rgb_out & 0x00FFFFFF);
	dst << pixel_out;

	if (pixel_in.last)
	{
		x = 0;
		y++;
	}
	else
	{
		x++;
	}

}

void stream(pixel_stream &src, pixel_stream &dst, int frame)
{
    cartoon_filter(src, dst,
                   100, // edge_thresh
                   288, // bright_factor
                   5);  // quant_levels
}

