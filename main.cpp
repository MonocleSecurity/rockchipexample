#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#define GLFW_EXPOSE_NATIVE_EGL

#include <cstring>
#include <drm/drm_fourcc.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <fcntl.h>
#include <fstream>
#include <GLES3/gl3.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <iostream>
#include <linux/dma-heap.h>
#include <memory>
#include <rga/im2d.hpp>
#include <rga/im2d_buffer.h>
#include <rga/rga.h>
#include <rga/RgaUtils.h>
#include <rga/RockchipRga.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/rk_type.h>
#include <rockchip/vpu_api.h>
#include <rockchip/mpp_err.h>
#include <rockchip/mpp_task.h>
#include <rockchip/mpp_meta.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/rk_mpi_cmd.h>
#include <sys/ioctl.h>
#include <thread>
#include <vector>

//TODO not sure which one to use
#define DMA_HEAP_UNCACHE_PATH           "/dev/dma_heap/system-uncached"
#define DMA_HEAP_PATH                   "/dev/dma_heap/system"
#define DMA_HEAP_DMA32_UNCACHE_PATCH    "/dev/dma_heap/system-uncached-dma32"
#define DMA_HEAP_DMA32_PATCH            "/dev/dma_heap/system-dma32"
#define CMA_HEAP_UNCACHE_PATH           "/dev/dma_heap/cma-uncached"
#define RV1106_CMA_HEAP_PATH	        "/dev/rk_dma_heap/rk-dma-heap-cma"

int dma_buf_alloc(const char *path, size_t size, int *fd, void **va) {
    int ret;
    int prot;
    void *mmap_va;
    int dma_heap_fd = -1;
    struct dma_heap_allocation_data buf_data;

    /* open dma_heap fd */
    dma_heap_fd = open(path, O_RDWR);
    if (dma_heap_fd < 0) {
        printf("open %s fail!\n", path);
        return dma_heap_fd;
    }

    /* alloc buffer */
    memset(&buf_data, 0x0, sizeof(struct dma_heap_allocation_data));

    buf_data.len = size;
    buf_data.fd_flags = O_CLOEXEC | O_RDWR;
    ret = ioctl(dma_heap_fd, DMA_HEAP_IOCTL_ALLOC, &buf_data);
    if (ret < 0) {
        printf("RK_DMA_HEAP_ALLOC_BUFFER failed\n");
        return ret;
    }

    /* mmap va */
    if (fcntl(buf_data.fd, F_GETFL) & O_RDWR)
        prot = PROT_READ | PROT_WRITE;
    else
        prot = PROT_READ;

    /* mmap contiguors buffer to user */
    mmap_va = (void *)mmap(NULL, buf_data.len, prot, MAP_SHARED, buf_data.fd, 0);
    if (mmap_va == MAP_FAILED) {
        printf("mmap failed: %s\n", strerror(errno));
        return -errno;
    }

    *va = mmap_va;
    *fd = buf_data.fd;

    close(dma_heap_fd);

    return 0;
}

int main()
{
  // Setup window
  if (!glfwInit())
  {
    std::cout << "Failed to initialise glfw" << std::endl;
    return -1;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_DECORATED, true);
  glfwWindowHint(GLFW_RESIZABLE, true);
  GLFWwindow* window = glfwCreateWindow(1024, 768, "Monocle", nullptr, nullptr);
  if (!window)
  {
    std::cout << "Failed to create window" << std::endl;
    return -2;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // Enable vsync
  // Read frame
  std::cout << "Loading frame" << std::endl;
  std::vector<char> buffer;
  {
    std::ifstream frame("frame.dat");
    if (!frame.is_open())
    {
      std::cout << "Failed to find frame" << std::endl;
      return -3;
    }
    buffer = std::vector<char>((std::istreambuf_iterator<char>(frame)), std::istreambuf_iterator<char>());
  }
  std::cout << "Frame size: " << buffer.size() << std::endl;
  // Sstup decoder
  MppCtx context;
  MppApi* api;
  MppPacket packet;
  MppBufferGroup frame_group;
  std::unique_ptr<char[]> packet_buffer = std::make_unique<char[]>(buffer.size());
  std::cout << "Initialising packet" << std::endl;
  int ret = mpp_packet_init(&packet, packet_buffer.get(), buffer.size());
  if (ret)
  {
    return -4;
  }
  std::cout << "Creating MPP context" << std::endl;
  ret = mpp_create(&context, &api);
  if (ret != MPP_OK)
  {
    return -5;
  }
  std::cout << "Configuring decoder" << std::endl;
  MpiCmd mpi_cmd = MPP_DEC_SET_PARSER_SPLIT_MODE;
  RK_U32 need_split = 1;
  MppParam param = &need_split;
  ret = api->control(context, mpi_cmd, param);
  if (ret != MPP_OK)
  {
    return  -6;
  }
  std::cout << "Creating decoder" << std::endl;
  ret = mpp_init(context, MPP_CTX_DEC, MPP_VIDEO_CodingAVC);
  if (ret != MPP_OK)
  {
    return -7;
  }
  // Decode frame
  MppFrame source_frame = nullptr;
  size_t pos = 0;
  while (pos < (buffer.size() - 4))
  {
    if ((buffer[pos] == 0) && (buffer[pos + 1] == 0) && (buffer[pos + 2] == 0) && (buffer[pos + 3] == 1))
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      size_t end_pos = pos;
      while (end_pos < (buffer.size() - 5))
      {
        if ((buffer[end_pos + 1] == 0) && (buffer[end_pos + 2] == 0) && (buffer[end_pos + 3] == 0) && (buffer[end_pos + 4] == 1))
        {
          break;
        }
        ++end_pos;
      }
      const size_t size = (end_pos - pos) + 1;
      std::cout << "Building packet: " << pos << " " << end_pos << " " << size << std::endl;
      memcpy(packet_buffer.get(), buffer.data() + pos, size);
      mpp_packet_write(packet, 0, packet_buffer.get(), size);
      mpp_packet_set_pos(packet, packet_buffer.get());
      mpp_packet_set_length(packet, size);
      std::cout << "Place packet" << std::endl;
      ret = api->decode_put_packet(context, packet);
      if (ret != MPP_OK)
      {
        std::cout << "Failed to place packet: " << ret << std::endl;
        continue;
      }
      std::cout << "Retrieve frame" << std::endl;
      ret = api->decode_get_frame(context, &source_frame);
      if (source_frame)
      {
        if (mpp_frame_get_info_change(source_frame))
        {
          std::cout << "Frame dimensions and format changed" << std::endl;
          ret = mpp_buffer_group_get_internal(&frame_group, MPP_BUFFER_TYPE_DRM);
          if (ret)
          {
            std::cout << "Failed to set buffer group" << std::endl;
            return -8;
          }
          api->control(context, MPP_DEC_SET_EXT_BUF_GROUP, frame_group);
          api->control(context, MPP_DEC_SET_INFO_CHANGE_READY, nullptr);
        }
        else
        {
          std::cout << "Retrieved frame" << std::endl;
          MppBuffer mpp_buffer = mpp_frame_get_buffer(source_frame);
          if (mpp_buffer == nullptr)
          {
            std::cout << "Failed to retrieve buffer from frame" << std::endl;
            return -9;
          }
          // Retrieve DMA version of the buffer
          const int fd = mpp_buffer_get_fd(mpp_buffer);
          const size_t size = mpp_buffer_get_size(mpp_buffer);
          const RK_U32 width = mpp_frame_get_width(source_frame);
          const RK_U32 height = mpp_frame_get_height(source_frame);
          const MppFrameFormat format = mpp_frame_get_fmt(source_frame);
          const RK_U32 offset_x = mpp_frame_get_offset_x(source_frame);
          const RK_U32 offset_y = mpp_frame_get_offset_y(source_frame);
          const RK_U32 hor_stride = mpp_frame_get_hor_stride(source_frame);
          const RK_U32 ver_stride = mpp_frame_get_ver_stride(source_frame);
//          const rga_buffer_handle_t dma_handle = importbuffer_fd(fd, size);
//          const rga_buffer_t dma_buffer = wrapbuffer_handle(dma_handle, width, height, RK_FORMAT_YCbCr_422_SP);
          // Create a destination DMA buffer
//          const size_t dst_buffer_size = width * height * 3;


//TODO
//          int dst_dma_fd = 0;
//          char* dst_buf = nullptr;
//          if (dma_buf_alloc(DMA_HEAP_UNCACHE_PATH, dst_buffer_size, &dst_dma_fd, (void **)&dst_buf))
          {
//            std::cout << "Failed to allocate DMA destination buffer" << std::endl;
//            return -10;
          }
          








/*TODO          char* dst_buf = reinterpret_cast<char*>(malloc(dst_buffer_size));
          rga_buffer_handle_t dst_handle = importbuffer_virtualaddr(dst_buf, dst_buffer_size);
          if (dst_handle == 0)
          {
            std::cout << "Failed to create destination buffer" << std::endl;
            return -10;
          }
          const rga_buffer_t dst_buffer = wrapbuffer_handle(dst_handle, width, height, RK_FORMAT_RGB_888);*/




//std::cout << dma_handle << " " << dma_buffer.fd << " " << dma_buffer.width << " " << dma_buffer.height << std::endl;//TODO
//std::cout << dst_buffer.fd << " " << dst_buffer.width << " " << dst_buffer.height << std::endl;//TODO



//TODO




          // Convert buffer to RGB888
 

//TODO




          // Create EGL image
          std::cout << "Creating EGL image: " << width << " " << height << " " << format << " " << offset_x << " " << offset_y << " " << hor_stride << " " << ver_stride << std::endl;
          if (format != MPP_FMT_YUV420SP)
          {
            std::cout << "Invalid frame format" << std::endl;
            return -10;
          }
//          EGLAttrib atts[] = {
          EGLint atts[] = {
                               EGL_WIDTH, width,
                               EGL_HEIGHT, height,
                               EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_NV12,
                               EGL_DMA_BUF_PLANE0_FD_EXT, fd,
                               EGL_DMA_BUF_PLANE0_OFFSET_EXT, offset_x,
                               EGL_DMA_BUF_PLANE0_PITCH_EXT, hor_stride,
                               EGL_DMA_BUF_PLANE1_FD_EXT, fd,
                               EGL_DMA_BUF_PLANE1_OFFSET_EXT, offset_x + (hor_stride * height),
                               EGL_DMA_BUF_PLANE1_PITCH_EXT, hor_stride,
//                               EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_RGB888,
//                               EGL_DMA_BUF_PLANE0_FD_EXT, dst_dma_fd,
//                               EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
//                               EGL_DMA_BUF_PLANE0_PITCH_EXT, width,//TODO maybe pitch/stride?
                               EGL_NONE
                             };
//          const EGLImageKHR egl_image = eglCreateImage(glfwGetEGLDisplay(), glfwGetEGLContext(window), EGL_LINUX_DMA_BUF_EXT, nullptr, atts); // Fails to create image here!!!

//TODO
          PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHRt = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
          const EGLImageKHR egl_image = eglCreateImageKHRt(glfwGetEGLDisplay(), EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, atts); // Fails to create image here!!!
          if (egl_image == EGL_NO_IMAGE_KHR)
          {
            std::cout << "Failed to create EGL image" << std::endl;
            return -11;
          }
          std::cout << "Successfully created EGL image" << std::endl;
          return 0;
        }
      }
    }
    ++pos;
  }
  // Main loop
  while (!glfwWindowShouldClose(window))
  {
    glfwPollEvents();
  }
  // Exit
  return 0;
}

