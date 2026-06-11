# FPGA Posterization Filter

Course: Reconfigurable Computing Design (CESE4090), TU Delft
Authors (Group 4): Mingwu Liu, Sofie Vos, Hruday Ramachandra

A real-time posterization video filter for the PYNQ-Z2 FPGA platform. A custom
hardware accelerator is inserted into an HDMI video pipeline to apply a
cartoon-like posterization effect on 1280x720 video at 60 fps.

## What It Does

The filter flattens smooth gradients into bands of color while preserving strong
edges:
1. Convert RGB to grayscale and denoise for edge extraction.
2. Detect edges (Sobel) to build a binary edge map.
3. Blur the RGB channels and scale brightness.
4. Quantize each channel to a reduced number of levels.
5. Overlay the edge map: edges become black, other pixels keep quantized color.

## Files

- `poserization_filter.cpp` — HLS source for the hardware accelerator.
- `posterization_filter.bit` — compiled FPGA bitstream.
- `posterization_filter.hwh` — Vivado hardware description file.
- `posterization_hardware_implementation_demonstration.ipynb` — hardware demo notebook.
- `python_algorithm_evaluation.ipynb` — CPU reference implementation and benchmark.
- `Reconfigurable_Computing_Design.pdf` — full project report.

## Platform

Xilinx Zynq-7000 SoC on the PYNQ-Z2 board: Python/Jupyter control from the ARM PS,
custom acceleration in the PL, AXI-Stream for the HDMI pixel flow, and AXI-Lite for
runtime parameters (edge threshold, brightness factor, quantization levels).

## Running the Demo

1. Copy `posterization_filter.bit` and the demo notebook to the PYNQ board.
2. Open `posterization_hardware_implementation_demonstration.ipynb` in Jupyter.
3. Load the overlay and set the filter registers (`edge_threshold`, `bright_factor`,
   `quant_levels`).
4. Start `hdmi_in` and `hdmi_out` to route the HDMI signal through the accelerator.
5. Run the benchmarking loop to measure per-frame latency and FPS.

The `python_algorithm_evaluation.ipynb` notebook provides a CPU-based reference
(OpenCV) for comparing software vs. FPGA-accelerated performance.