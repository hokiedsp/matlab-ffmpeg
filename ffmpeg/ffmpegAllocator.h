#pragma once

// CUSTOM STL allocator using the Intel MKL memory management functions
extern "C" {
#include <libavutil/mem.h>
}
#include <memory>

template <typename T>
class ffmpegAllocator : public std::allocator<T>
{
public:
  ffmpegAllocator() {}
  ffmpegAllocator &operator=(const ffmpegAllocator &rhs)
  {
    std::allocator<T>::operator=(rhs);
    return *this;
  }

  pointer allocate(size_type n, const void *hint = NULL)
  {
    pointer p = NULL;
    size_t count = n*sizeof(T);
    if (!hint)
    {
      p = reinterpret_cast<pointer>(av_malloc(count));
    }
    else
    {
      p = reinterpret_cast<pointer>(av_realloc((void *)hint, count));
    }
    return p;
  }

  void deallocate(pointer p, size_type n)
  {
    mxFree(p);
  }

  void construct(pointer p, const T &val)
  {
    new (p) T(val);
  }

  void destroy(pointer p)
  {
    p->~T();
  }
};
