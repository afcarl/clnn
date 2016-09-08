// Use 1024 threads per block, which requires cuda sm_2x or above
//const int CL_NUM_THREADS = 1024;

#include "utils.h"
#include "luaT.h"
#include "THClTensor.h"
#include "THClTensorMath.h"
#include "THClBlas.h"
#include "THClKernels.h"
#include "templates/TemplatedKernel.h"
#include "DeviceInfo.h"
#include "EasyCL.h"
#include "im2col.h"

static std::string getKernelTemplate();

inline int getNumThreads(THClState *state) {
  int blockSize = 1024;
  int maxWorkgroupSize = ((easycl::DeviceInfo *)state->deviceInfoByDevice[state->currentDevice])->maxWorkGroupSize;
  if( blockSize > maxWorkgroupSize ) {
    blockSize = maxWorkgroupSize;
  }
  return blockSize;
}

std::string SpatialConvolutionMM_getKernelTemplate();

// CL: number of blocks for threads.
inline int GET_BLOCKS(THClState *state, const int N) {
  return (N + getNumThreads(state) - 1) / getNumThreads(state);
}

void im2col(THClState *state, THClTensor* im, const int channels,
    const int height, const int width, const int ksize_h, const int ksize_w, const int pad_h,
    const int pad_w, const int stride_h, const int stride_w, THClTensor* col) {
  // We are going to launch channels * height_col * width_col kernels, each
  // kernel responsible for copying a single-channel grid.
  int height_col = (height + 2 * pad_h - ksize_h) / stride_h + 1;
  int width_col = (width + 2 * pad_w - ksize_w) / stride_w + 1;
  int num_kernels = channels * height_col * width_col;

  std::string uniqueName = "im2col";
  EasyCL *cl = im->storage->cl;
  CLKernel *kernel = 0;
  if(cl->kernelExists(uniqueName)) {
    kernel = cl->getKernel(uniqueName);
  } else {
    TemplatedKernel kernelBuilder(cl);
    kernel = kernelBuilder.buildKernel(uniqueName, "im2col.cl",
      getKernelTemplate(), "im2col_kernel");
  }

  THClKernels k(state, kernel);
  k.in(num_kernels);
  k.in(im);
  k.in(height);
  k.in(width);
  k.in(ksize_h);
  k.in(ksize_w);
  k.in(pad_h);
  k.in(pad_w);
  k.in(stride_h);
  k.in(stride_w);
  k.in(height_col);
  k.in(width_col);
  k.out(col);

  k.run(GET_BLOCKS(state, num_kernels), getNumThreads(state));

  // Launch
//  im2col_kernel <<<GET_BLOCKS(num_kernels), CL_NUM_THREADS, 0, stream>>> (
//      num_kernels, data_im, height, width, ksize_h, ksize_w,
//      pad_h, pad_w, stride_h, stride_w,
//      height_col, width_col, data_col
//  );
}

void col2im(THClState *state, THClTensor* col, const int channels,
    const int height, const int width, const int patch_h, const int patch_w, const int pad_h,
    const int pad_w, const int stride_h, const int stride_w, THClTensor* im) {
  int height_col = (height + 2 * pad_h - patch_h) / stride_h + 1;
  int width_col = (width + 2 * pad_w - patch_w) / stride_w + 1;
  int num_kernels = channels * height * width;
  // To avoid involving atomic operations, we will launch one kernel per
  // bottom dimension, and then in the kernel add up the top dimensions.

  EasyCL *cl = im->storage->cl;
  std::string uniqueName = "col2im";
  CLKernel *kernel = 0;
  if(cl->kernelExists(uniqueName)) {
    kernel = cl->getKernel(uniqueName);
  } else {
    TemplatedKernel kernelBuilder(cl);
    kernel = kernelBuilder.buildKernel(uniqueName, "im2col.cl",
      getKernelTemplate(), "col2im_kernel");
  }

  THClKernels k(state, kernel);
  k.in(num_kernels);
  k.in(col);
  k.in(height);
  k.in(width);
  k.in(channels);

  k.in(patch_h);
  k.in(patch_w);
  k.in(pad_h);
  k.in(pad_w);
  k.in(stride_h);
  k.in(stride_w);

  k.in(height_col);
  k.in(width_col);
  k.out(im);

  k.run(GET_BLOCKS(state, num_kernels), getNumThreads(state));

//  col2im_kernel <<<GET_BLOCKS(num_kernels), CL_NUM_THREADS, 0, stream>>> (
//      num_kernels, data_col, height, width, channels,
//      patch_h, patch_w, pad_h, pad_w, stride_h, stride_w,
//      height_col, width_col, data_im
//  );
//  THError("Not implemented");
}

#undef CL_KERNEL_LOOP

std::string getKernelTemplate() {
  // [[[cog
  // import stringify
  // stringify.write_kernel( "kernel", "lib/THCLNN/im2col.cl" )
  // ]]]
  // generated using cog, from lib/THCLNN/im2col.cl:
  const char * kernelSource =  
  "// from im2col.h:\n"
  "\n"
  "// CL: grid stride looping\n"
  "#define CL_KERNEL_LOOP(i, n)                        \\\n"
  "  for (int i = get_group_id(0) * get_local_size(0) + get_local_id(0); \\\n"
  "      i < (n);                                       \\\n"
  "      i += get_local_size(0) * get_num_groups(0))\n"
  "\n"
  "// Kernel for fast unfold+copy\n"
  "// (borrowed from Caffe: https://github.com/BVLC/caffe/blob/master/src/caffe/layers/conv_layer.cu)\n"
  "kernel void im2col_kernel(const int n, const global float* im_data, int im_offset,\n"
  "    const int height, const int width, const int ksize_h, const int ksize_w, const int pad_h,\n"
  "    const int pad_w, const int stride_h, const int stride_w, const int height_col, const int width_col,\n"
  "    global float* col_data, int col_offset) {\n"
  "  global const float *data_im = im_data + im_offset;\n"
  "  global float *data_col = col_data + col_offset;\n"
  "\n"
  "  CL_KERNEL_LOOP(index, n) {\n"
  "    int w_out = index % width_col;\n"
  "    index /= width_col;\n"
  "    int h_out = index % height_col;\n"
  "    int channel_in = index / height_col;\n"
  "    int channel_out = channel_in * ksize_h * ksize_w;\n"
  "    int h_in = h_out * stride_h - pad_h;\n"
  "    int w_in = w_out * stride_w - pad_w;\n"
  "    data_col += (channel_out * height_col + h_out) * width_col + w_out;\n"
  "    data_im += (channel_in * height + h_in) * width + w_in;\n"
  "    for (int i = 0; i < ksize_h; ++i) {\n"
  "      for (int j = 0; j < ksize_w; ++j) {\n"
  "        int h = h_in + i;\n"
  "        int w = w_in + j;\n"
  "        *data_col = (h >= 0 && w >= 0 && h < height && w < width) ?\n"
  "          data_im[i * width + j] : 0;\n"
  "        data_col += height_col * width_col;\n"
  "      }\n"
  "    }\n"
  "  }\n"
  "}\n"
  "\n"
  "kernel void col2im_kernel(const int n, global const float* col_data, int col_offset,\n"
  "    const int height, const int width, const int channels, const int patch_h, const int patch_w,\n"
  "    const int pad_h, const int pad_w, const int stride_h, const int stride_w,\n"
  "    const int height_col, const int width_col,\n"
  "    global float* im_data, int im_offset) {\n"
  "  global float *data_im = im_data + im_offset;\n"
  "  global const float *data_col = col_data + col_offset;\n"
  "\n"
  "  CL_KERNEL_LOOP(index, n) {\n"
  "    float val = 0;\n"
  "    int w = index % width + pad_w;\n"
  "    int h = (index / width) % height + pad_h;\n"
  "    int c = index / (width * height);\n"
  "    // compute the start and end of the output\n"
  "    int w_col_start = (w < patch_w) ? 0 : (w - patch_w) / stride_w + 1;\n"
  "    int w_col_end = min(w / stride_w + 1, width_col);\n"
  "    int h_col_start = (h < patch_h) ? 0 : (h - patch_h) / stride_h + 1;\n"
  "    int h_col_end = min(h / stride_h + 1, height_col);\n"
  "    /*\n"
  "       for (int h_col = h_col_start; h_col < h_col_end; ++h_col) {\n"
  "       for (int w_col = w_col_start; w_col < w_col_end; ++w_col) {\n"
  "    // the col location: [c * width * height + h_out, w_out]\n"
  "    int c_col = c * patch_h * patch_w + (h - h_col * stride_h) * ksize + (w - w_col * stride_w);\n"
  "    val += data_col[(c_col * height_col + h_col) * width_col + w_col];\n"
  "    }\n"
  "    }\n"
  "     */\n"
  "    // equivalent implementation\n"
  "    int offset = (c * patch_h * patch_w + h * patch_w + w) * height_col * width_col;\n"
  "    int coeff_h_col = (1 - stride_h * patch_w * height_col) * width_col;\n"
  "    int coeff_w_col = (1 - stride_w * height_col * width_col);\n"
  "    for (int h_col = h_col_start; h_col < h_col_end; ++h_col) {\n"
  "      for (int w_col = w_col_start; w_col < w_col_end; ++w_col) {\n"
  "        val += data_col[offset + h_col * coeff_h_col + w_col * coeff_w_col];\n"
  "      }\n"
  "    }\n"
  "    data_im[index] = val;\n"
  "  }\n"
  "}\n"
  "\n"
  "";
  // [[[end]]]
  return kernelSource;
}

