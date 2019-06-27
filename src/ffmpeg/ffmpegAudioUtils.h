#pragma once

/**
 * @file
 * Wrappers for FFmpeg's misc image utilities <libavutil/imgutils.h> and
 * other image-related utility functions.
 *
 * @addtogroup lavu_picture
 * @{
 */

#include "ffmpegException.h"

extern "C"
{
#include <libavutil/samplefmt.h>
}

namespace ffmpeg
{

// #define AV_NUM_DATA_POINTERS 8
/**
 * pointer to the picture/channel planes.
 * This might be different from the first allocated byte
 *
 * Some decoders access areas outside 0,0 - width,height, please
 * see avcodec_align_dimensions2(). Some filters and swscale can read
 * up to 16 bytes beyond the planes, if these filters are to be used,
 * then 16 extra bytes must be allocated.
 *
 * NOTE: Except for hwaccel formats, pointers not needed by the format
 * MUST be set to NULL.
 */
// uint8_t *data[AV_NUM_DATA_POINTERS];

/**
 * For audio, size in bytes of each plane.
 *
 * For audio, only linesize[0] may be set. For planar audio, each channel
 * plane must be the same size.
 *
 * @note The linesize may be larger than the size of usable data -- there
 * may be extra padding present for performance reasons.
 */
// int linesize[AV_NUM_DATA_POINTERS];

/**
 * pointers to the data planes/channels.
 *
 * For planar audio, each channel has a separate data pointer, and
 * linesize[0] contains the size of each channel buffer.
 * For packed audio, there is just one data pointer, and linesize[0]
 * contains the total size of the buffer for all channels.
 *
 * Note: Both data and extended_data should always be set in a valid frame,
 * but for planar audio with more channels that can fit in data,
 * extended_data must be used in order to access all channels.
 */
// uint8_t **extended_data;

/**
 * number of audio samples (per channel) described by this frame
 */
// int nb_samples;

/**
 * format of the frame, -1 if unknown or unset
 * Values correspond to enum AVPixelFormat for video frames,
 * enum AVSampleFormat for audio)
 */
// int format;

/**
 * Presentation timestamp in time_base units (time when frame should be shown to
 * user).
 */
// int64_t pts;

/**
 * DTS copied from the AVPacket that triggered returning this frame. (if frame
 * threading isn't used) This is also the Presentation time of this AVFrame
 * calculated from only AVPacket.dts values without pts values.
 */
// int64_t pkt_dts;

/**
 * Tell user application that palette has changed from previous frame.
 */
// int palette_has_changed;

/**
 * Sample rate of the audio data.
 */
// int sample_rate;

/**
 * Channel layout of the audio data.
 */
// uint64_t channel_layout;

/**
 * AVBuffer references backing the data for this frame. If all elements of
 * this array are NULL, then this frame is not reference counted. This array
 * must be filled contiguously -- if buf[i] is non-NULL then buf[j] must
 * also be non-NULL for all j < i.
 *
 * There may be at most one AVBuffer per data plane, so for video this array
 * always contains all the references. For planar audio with more than
 * AV_NUM_DATA_POINTERS channels, there may be more buffers than can fit in
 * this array. Then the extra AVBufferRef pointers are stored in the
 * extended_buf array.
 */
// AVBufferRef *buf[AV_NUM_DATA_POINTERS];

/**
 * For planar audio which requires more than AV_NUM_DATA_POINTERS
 * AVBufferRef pointers, this array will hold all the references which
 * cannot fit into AVFrame.buf.
 *
 * Note that this is different from AVFrame.extended_data, which always
 * contains all the pointers. This array only contains the extra pointers,
 * which cannot fit into AVFrame.buf.
 *
 * This array is always allocated using av_malloc() by whoever constructs
 * the frame. It is freed in av_frame_unref().
 */
// AVBufferRef **extended_buf;
/**
 * Number of elements in extended_buf.
 */
// int        nb_extended_buf;

/**
 * frame timestamp estimated using various heuristics, in stream time base
 * - encoding: unused
 * - decoding: set by libavcodec, read by user.
 */
// int64_t best_effort_timestamp;

/**
 * duration of the corresponding packet, expressed in
 * AVStream->time_base units, 0 if unknown.
 * - encoding: unused
 * - decoding: Read by user.
 */
// int64_t pkt_duration;

/**
 * decode error flags of the frame, set to a combination of
 * FF_DECODE_ERROR_xxx flags if the decoder produced a frame, but there
 * were errors during the decoding.
 * - encoding: unused
 * - decoding: set by libavcodec, read by user.
 */
// int decode_error_flags;

/**
 * number of audio channels, only used for audio.
 * - encoding: unused
 * - decoding: Read by user.
 */
// int channels;

/**
 * \brief Returns the size in bytes of the amount of data required to store an
 * image with the given parameters.
 *
 * Returns the size in bytes to store the image data in a component-separate
 * buffer. FFmpeg video frames (or most video frames for that matter) are
 * typically bit-packed. A component-separate buffer unpack these frame data so
 * that each color component is stored in one byte per pixel.
 *
 * @param[in] pix_desc      pointer to the descriptor of the image's pixel
 * format
 * @param[in] width         the width of the image in pixels
 * @param[in] height        the height of the image in pixels
 * @param[in] dst_linesize  [optional] the destination linesize; uses \ref width
 * if not given or =0;
 * @return the buffer size in bytes
 * @throws Exception if unsupported pixel format
 * @throws Exception if any component of the pixel format does not fit in a byte
 * @throws Exception if non-zero dst_linesize < width
 */
inline int audioGetComponentBufferSize(AVSampleFormat sample_fmt,
                                       int nb_channels, int nb_samples,
                                       const int dst_linesize = 0,
                                       bool align = true)
{
  int linesize;
  int bufsize = av_samples_get_buffer_size(&linesize, nb_channels, nb_samples,
                                           sample_fmt, align);

  if (dst_linesize && dst_linesize < linesize)
    throw Exception("[ffmpeg::audioGetComponentBufferSize] Destination "
                    "linesize (%d) too small (must %d)",
                    dst_linesize, width);

  return (dst_linesize ? dst_linesize : width) * height *
         pix_desc->nb_components;

  /**
   * Get the required buffer size for the given audio parameters.
   *
   * @param[out] linesize calculated linesize, may be NULL
   * @param nb_channels   the number of channels
   * @param nb_samples    the number of samples in a single channel
   * @param sample_fmt    the sample format
   * @param align         buffer size alignment (0 = default, 1 = no alignment)
   * @return              required buffer size, or negative error code on
   * failure
   */
}

/**
 * \brief Returns the size in bytes of the amount of data required to store an
 * image with the given parameters.
 *
 * Returns the size in bytes to store the image data in a component-separate
 * buffer. FFmpeg video frames (or most video frames for that matter) are
 * typically bit-packed. A component-separate buffer unpack these frame data so
 * that each color component is stored in one byte per pixel.
 *
 * @param[in] pix_fmt       the pixel format of the image
 * @param[in] width         the width of the image in pixels
 * @param[in] height        the height of the image in pixels
 * @param[in] dst_linesize  [optional] the destination linesize; uses \ref width
 * if not given or =0;
 * @return the buffer size in bytes
 * @throws Exception if unsupported pixel format
 * @throws Exception if any component of the pixel format does not fit in a byte
 * @throws Exception if non-zero dst_linesize < width
 */
inline int imageGetComponentBufferSize(const AVPixelFormat pix_fmt,
                                       const int width, const int height,
                                       const int dst_linesize = 0)
{
  return imageGetComponentBufferSize(av_pix_fmt_desc_get(pix_fmt), width,
                                     height, dst_linesize);
}

/**
 * \brief Get a pixel value from an AVFrame image buffer
 *
 * imageGetComponentPixelValue macro shall be used to retrieve the pixel
 * component value during the pix_op callback function in
 * imageForEachComponentPixel() or imageForEachConstComponentPixel(), with the
 * corresponding inputs argument thereof.
 *
 * \param[in] AVFrame buffer data bit-packed value
 * \param[in] Number of least significant bits that must be shifted away to get
 * the value. \param[in] Bit mask to obtain the pixel component value after
 * shift. \returns   The pixel component value
 */
#define imageGetComponentPixelValue(data, shift, mask)                         \
  (((data) >> (shift)) & (mask))

/**
 * \brief Set a pixel value on an AVFrame image buffer
 *
 * imageSetComponentPixelValue macro shall be used to set the pixel component
 * value on an AVFrame image buffer during the pix_op callback function in
 * imageForEachComponentPixel() or imageForEachConstComponentPixel(), with the
 * corresponding inputs argument thereof.
 *
 * \param[out] AVFrame image buffer element to write the component pixel value
 * to \param[in]  AVFrame buffer data bit-packed value \param[in]  Number of
 * least significant bits that must be shifted away to get the value. \param[in]
 * Bit mask to obtain the pixel component value after shift. \param[in]  The
 * pixel component value to set
 */
#define imageSetComponentPixelValue(data, shift, mask, value)                  \
  (data) &= (mask);                                                            \
  (data) |= (((value) & (mask)) << (shift))

// int av_image_copy_to_buffer(uint8_t *dst, int dst_size,
//                             const uint8_t * const src_data[4], const int
//                             src_linesize[4], enum AVPixelFormat pix_fmt, int
//                             width, int height, int align);

/* internal use only */
#define FOR_EACH_COMPONENT_PIXEL_MACRO(DataType)                               \
  {                                                                            \
    bool ok = true;                                                            \
    for (int i = 0; ok && i < pix_desc->nb_components; ++i)                    \
    {                                                                          \
      const AVComponentDescriptor &d = pix_desc->comp[i];                      \
      int j = d.plane;                                                         \
      int L = linesize[j];                                                     \
      DataType *comp = img_data[j];                                            \
      DataType *comp_end = comp + height * L;                                  \
      DataType mask = (DataType(1) << d.depth) - 1;                            \
      for (; ok && comp < comp_end; comp += L)                                 \
      {                                                                        \
        DataType *line = comp + d.offset;                                      \
        for (int w = 0; w < width; ++w)                                        \
        {                                                                      \
          ok = pix_op(*line, d.shift, mask);                                   \
          line += d.step;                                                      \
        }                                                                      \
        ok = eol_op();                                                         \
      }                                                                        \
      ok = eoc_op();                                                           \
    }                                                                          \
  }

/**
 * \brief Template function to iterate over pixels of image components
 *
 * Template function to traverse through all the pixel components of image data.
 * Many programs that work on the video/image data obtained from FFmpeg API
 * often deal with individual pixel component values rather than a bit-packed
 * video formats. While the most efficient approach is to use the ``format''
 * filter to convert it to a byte-sized format so the data values are easy to be
 * pulled apart, imageForEachComponentPixel() provides a way to access the pixel
 * values of an arbitrary formats with 3 function objects: one for a pixel
 * operation, another for the end-of-line operation, and the last for the
 * end-of-component operation. The latter two are default to no-op.
 *
 * \note To access the pixel value
 *
 * \note This function presents pixels in the order of width, height, then
 * component.
 *
 * @param[in] img_data      pointers containing the source image data
 * @param[in] linesize      linesizes for the image in img_data
 * @param[in] pix_desc      pointer to the descriptor of the image's pixel
 * format
 * @param[in] width         the width of the source image in pixels
 * @param[in] height        the height of the source image in pixels
 * @param[in] pix_op        pixel operation function object that will be
 * applied. The signature of the function should be equivalent to the following:
 *                          @code
 *                            bool fun(const DataType &data, const int shift,
 * const DataType mask);
 *                          @endcode
 *                          The signature may not need to have const &,
 * depending on DataType. The function returns true to continue or false to
 *                          terminate the loop.
 * @param[in] eol_op        [optional] end-of-line function object to be called
 * at the end of each line. This function does not take any arguments but
 * returns true to continue or false to terminate the loop.
 * @param[in] eoc_op        [optional] end-of-component function object to be
 * called at the end of each component. This function does not take any
 *                          arguments but returns true to continue or false to
 * terminate the loop.
 * @return                  the number of bytes written to dst
 */
template <class PixelOperation, class EolOperation, class EocOperation>
void imageForEachComponentPixel(uint8_t *const img_data[4],
                                const int linesize[4],
                                const AVPixFmtDescriptor *pix_desc,
                                const int width, const int height,
                                PixelOperation pix_op,
                                EolOperation eol_op = []() {},
                                EocOperation eoc_op = []() {})
{
  FOR_EACH_COMPONENT_PIXEL_MACRO(uint8_t)
}

/**
 * \brief Template function to iterate over pixels of image components
 *
 * Template function to traverse through all the pixel components of image data.
 * Many programs that work on the video/image data obtained from FFmpeg API
 * often deal with individual pixel component values rather than a bit-packed
 * video formats. While the most efficient approach is to use the ``format''
 * filter to convert it to a byte-sized format so the data values are easy to be
 * pulled apart, imageForEachComponentPixel() provides a way to access the pixel
 * values of an arbitrary formats with 3 function objects: one for a pixel
 * operation, another for the end-of-line operation, and the last for the
 * end-of-component operation. The latter two are default to no-op.
 *
 * \note To access the pixel value
 *
 * \note This function presents pixels in the order of width, height, then
 * component.
 *
 * @param[in] img_data      pointers containing the source image data
 * @param[in] linesize      linesizes for the image in img_data
 * @param[in] pix_desc      pointer to the descriptor of the image's pixel
 * format
 * @param[in] width         the width of the source image in pixels
 * @param[in] height        the height of the source image in pixels
 * @param[in] pix_op        pixel operation function object that will be
 * applied. The signature of the function should be equivalent to the following:
 *                          @code
 *                            bool fun(const DataType &data, const int shift,
 * const DataType mask);
 *                          @endcode
 *                          The signature may not need to have const &,
 * depending on DataType. The function returns true to continue or false to
 *                          terminate the loop.
 * @param[in] eol_op        [optional] end-of-line function object to be called
 * at the end of each line. This function does not take any arguments but
 * returns true to continue or false to terminate the loop.
 * @param[in] eoc_op        [optional] end-of-component function object to be
 * called at the end of each component. This function does not take any
 *                          arguments but returns true to continue or false to
 * terminate the loop.
 * @return                  the number of bytes written to dst
 */
template <class PixelOperation, class EolOperation, class EocOperation>
void imageForEachConstComponentPixel(
    const uint8_t *const img_data[4], const int linesize[4],
    const AVPixFmtDescriptor *pix_desc, const int width, const int height,
    PixelOperation pix_op, EolOperation eol_op = []() { return true; },
    EocOperation eoc_op = []() { return true; })
{
  FOR_EACH_COMPONENT_PIXEL_MACRO(const uint8_t)
}

/**
 * \brief Copy image data from an image into a component-buffer.
 *
 * imageCopyToComponentBuffer() copies the FFmpeg image data to in a
 * component-separate buffer. FFmpeg video frames (or most video frames for that
 * matter) are typically bit-packed. A component-separate buffer unpack these
 * frame data so that each color component is stored in one byte per pixel.
 *
 * imageGetComponentBufferSize() can be used to compute the required size for
 * the buffer to fill.
 *
 * @param dst[inout]        a buffer into which picture data will be copied
 * @param dst_size[in]      the size in bytes of dst
 * @param src_data[in]      pointers containing the source image data
 * @param src_linesize[in]  linesizes for the image in src_data
 * @param[in] pix_desc      pointer to the descriptor of the image's pixel
 * format
 * @param width[in]         the width of the source image in pixels
 * @param height[in]        the height of the source image in pixels
 * @param dst_linesize[in]  the linesize for dst; omit or <=0 to use width
 * @param dst_compsize[in]  the component size for dst; omit or <=0 ot use
 * dst_linesize*height
 * @return                  the number of bytes written to dst
 */
inline int imageCopyToComponentBuffer(uint8_t *dst, const int dst_size,
                                      const uint8_t *const src_data[4],
                                      const int src_linesize[4],
                                      const AVPixFmtDescriptor *pix_desc,
                                      const int width, const int height,
                                      int dst_linesize = 0,
                                      int dst_compsize = 0)
{
  if (dst_linesize <= 0) dst_linesize = width;
  if (dst_compsize <= 0) dst_compsize = dst_linesize * height;

  uint8_t *wr_end = dst + dst_size;
  uint8_t *wr_pos = dst;
  uint8_t *line = dst;
  uint8_t *px = line;
  imageForEachConstComponentPixel(
      src_data, src_linesize, pix_desc, width, height,
      [&](const uint8_t &src, const int shift, const uint8_t mask) {
        // Copy frame src
        *(px++) = imageGetComponentPixelValue(src, shift, mask);
        return px < wr_end;
      },
      [&]() {
        px = line += dst_linesize;
        return line < wr_end;
      },
      [&]() {
        px = line = wr_pos += dst_compsize;
        return wr_pos < wr_end;
      });
  return int(wr_pos - dst);
}

/**
 * \brief Copy image data from an image into a component-buffer.
 *
 * imageCopyToComponentBuffer() copies the FFmpeg image data to in a
 * component-separate buffer. FFmpeg video frames (or most video frames for that
 * matter) are typically bit-packed. A component-separate buffer unpack these
 * frame data so that each color component is stored in one byte per pixel.
 *
 * imageGetComponentBufferSize() can be used to compute the required size for
 * the buffer to fill.
 *
 * @param dst[inout]        a buffer into which picture data will be copied
 * @param dst_size[in]      the size in bytes of dst
 * @param src_data[in]      pointers containing the source image data
 * @param src_linesize[in]  linesizes for the image in src_data
 * @param pix_fmt[in]       the pixel format of the source image
 * @param width[in]         the width of the source image in pixels
 * @param height[in]        the height of the source image in pixels
 * @param dst_linesize[in]  the assumed linesize alignment for dst
 * @return                  the number of bytes written to dst
 */
inline int imageCopyToComponentBuffer(
    uint8_t *dst, const int dst_size, const uint8_t *const src_data[4],
    const int src_linesize[4], const AVPixelFormat pix_fmt, const int width,
    const int height, int dst_linesize = 0, int dst_compsize = 0)
{
  return imageCopyToComponentBuffer(dst, dst_size, src_data, src_linesize,
                                    av_pix_fmt_desc_get(pix_fmt), width, height,
                                    dst_linesize);
}

/**
 * \brief Copy image data from an image into a component-buffer.
 *
 * imageCopyFromComponentBuffer() copies the FFmpeg image data to in a
 * component-separate buffer. FFmpeg video frames (or most video frames for that
 * matter) are typically bit-packed. A component-separate buffer unpack these
 * frame data so that each color component is stored in one byte per pixel.
 *
 * imageGetComponentBufferSize() can be used to compute the required size for
 * the buffer to fill.
 *
 * @param[in] src           a buffer from which picture data will be copied
 * @param[in] src_size      the size in bytes of src
 * @param[inout] dst_data   pointers containing the destination image data
 * @param[in] dst_linesize  linesizes for the image in dst_data
 * @param[in] pix_desc      pointer to the descriptor of the image's pixel
 * format
 * @param width[in]         the width of the source image in pixels
 * @param height[in]        the height of the source image in pixels
 * @param src_linesize[in]  the linesize for src; omit or <=0 to use width
 * @param src_compsize[in]  the component size for src; omit or <=0 ot use
 * src_linesize*height
 * @return                  the number of bytes written to dst
 */
inline int imageCopyFromComponentBuffer(const uint8_t *src, const int src_size,
                                        uint8_t *const dst_data[4],
                                        const int dst_linesize[4],
                                        const AVPixFmtDescriptor *pix_desc,
                                        const int width, const int height,
                                        int src_linesize = 0,
                                        int src_compsize = 0)
{
  if (src_linesize <= 0) src_linesize = width;
  if (src_compsize <= 0) src_compsize = src_linesize * height;

  const uint8_t *rd_end = src + src_size;
  const uint8_t *rd_pos = src;
  const uint8_t *line = src;
  const uint8_t *px = line;
  imageForEachComponentPixel(
      dst_data, dst_linesize, pix_desc, width, height,
      [&](uint8_t &dst, const int shift, const uint8_t mask) {
        // Copy frame data
        imageSetComponentPixelValue(dst, shift, mask, *(px++));
        return px < rd_end;
      },
      [&]() {
        px = line += src_linesize;
        return line < rd_end;
      },
      [&]() {
        px = line = rd_pos += src_compsize;
        return rd_pos < rd_end;
      });
  return int(rd_pos - src);
}

/**
 * \brief Copy image data from a component-buffer to an image data buffers.
 *
 * imageCopyFromComponentBuffer() copies the FFmpeg image data from a
 * component-separate buffer. FFmpeg video frames (or most video frames for that
 * matter) are typically bit-packed. A component-separate buffer unpack these
 * frame data so that each color component is stored in one byte per pixel.
 *
 * @param[in] src           a buffer from which picture data will be copied
 * @param[in] src_size      the size in bytes of src
 * @param[inout] dst_data   pointers containing the destination image data
 * @param[in] dst_linesize  linesizes for the image in dst_data
 * @param[in] pix_desc      pointer to the descriptor of the image's pixel
 * format
 * @param width[in]         the width of the source image in pixels
 * @param height[in]        the height of the source image in pixels
 * @param src_linesize[in]  the linesize for src; omit or <=0 to use width
 * @param src_compsize[in]  the component size for src; omit or <=0 ot use
 * src_linesize*height
 * @return                  the number of bytes written to dst
 */
inline int imageCopyFromComponentBuffer(
    const uint8_t *src, const int src_size, uint8_t *const dst_data[4],
    const int dst_linesize[4], const AVPixelFormat pix_fmt, const int width,
    const int height, int src_linesize = 0, int src_compsize = 0)
{
  return imageCopyFromComponentBuffer(src, src_size, dst_data, dst_linesize,
                                      av_pix_fmt_desc_get(pix_fmt), width,
                                      height, src_linesize, src_compsize);
}

/**
 * Compute the max pixel step for each plane of an image with a
 * format described by pixdesc.
 *
 * The pixel step is the distance in bytes between the first byte of
 * the group of bytes which describe a pixel component and the first
 * byte of the successive group in the same plane for the same
 * component.
 *
 * @param max_pixsteps an array which is filled with the max pixel step
 * for each plane. Since a plane may contain different pixel
 * components, the computed max_pixsteps[plane] is relative to the
 * component in the plane with the max pixel step.
 * @param max_pixstep_comps an array which is filled with the component
 * for each plane which has the max pixel step. May be NULL.
 */
// void av_image_fill_max_pixsteps(int max_pixsteps[4], int
// max_pixstep_comps[4],
//                                 const AVPixFmtDescriptor *pixdesc);

/**
 * Compute the size of an image line with format pix_fmt and width
 * width for the plane plane.
 *
 * @return the computed size in bytes
 */
// int av_image_get_linesize(enum AVPixelFormat pix_fmt, int width, int plane);

/**
 * Fill plane linesizes for an image with pixel format pix_fmt and
 * width width.
 *
 * @param linesizes array to be filled with the linesize for each plane
 * @return >= 0 in case of success, a negative error code otherwise
 */
// int av_image_fill_linesizes(int linesizes[4], enum AVPixelFormat pix_fmt, int
// width);

/**
 * Fill plane data pointers for an image with pixel format pix_fmt and
 * height height.
 *
 * @param data pointers array to be filled with the pointer for each image plane
 * @param ptr the pointer to a buffer which will contain the image
 * @param linesizes the array containing the linesize for each
 * plane, should be filled by av_image_fill_linesizes()
 * @return the size in bytes required for the image buffer, a negative
 * error code in case of failure
 */
// int av_image_fill_pointers(uint8_t *data[4], enum AVPixelFormat pix_fmt, int
// height,
//                            uint8_t *ptr, const int linesizes[4]);

/**
 * Allocate an image with size w and h and pixel format pix_fmt, and
 * fill pointers and linesizes accordingly.
 * The allocated image buffer has to be freed by using
 * av_freep(&pointers[0]).
 *
 * @param align the value to use for buffer size alignment
 * @return the size in bytes required for the image buffer, a negative
 * error code in case of failure
 */
// int av_image_alloc(uint8_t *pointers[4], int linesizes[4],
//                    int w, int h, enum AVPixelFormat pix_fmt, int align);

/**
 * Copy image plane from src to dst.
 * That is, copy "height" number of lines of "bytewidth" bytes each.
 * The first byte of each successive line is separated by *_linesize
 * bytes.
 *
 * bytewidth must be contained by both absolute values of dst_linesize
 * and src_linesize, otherwise the function behavior is undefined.
 *
 * @param dst_linesize linesize for the image plane in dst
 * @param src_linesize linesize for the image plane in src
 */
// void av_image_copy_plane(uint8_t       *dst, int dst_linesize,
//                          const uint8_t *src, int src_linesize,
//                          int bytewidth, int height);

/**
 * Copy image in src_data to dst_data.
 *
 * @param dst_linesizes linesizes for the image in dst_data
 * @param src_linesizes linesizes for the image in src_data
 */
// void av_image_copy(uint8_t *dst_data[4], int dst_linesizes[4],
//                    const uint8_t *src_data[4], const int src_linesizes[4],
//                    enum AVPixelFormat pix_fmt, int width, int height);

/**
 * Copy image data located in uncacheable (e.g. GPU mapped) memory. Where
 * available, this function will use special functionality for reading from such
 * memory, which may result in greatly improved performance compared to plain
 * av_image_copy().
 *
 * The data pointers and the linesizes must be aligned to the maximum required
 * by the CPU architecture.
 *
 * @note The linesize parameters have the type ptrdiff_t here, while they are
 *       int for av_image_copy().
 * @note On x86, the linesizes currently need to be aligned to the cacheline
 *       size (i.e. 64) to get improved performance.
 */
// void av_image_copy_uc_from(uint8_t *dst_data[4],       const ptrdiff_t
// dst_linesizes[4],
//                            const uint8_t *src_data[4], const ptrdiff_t
//                            src_linesizes[4], enum AVPixelFormat pix_fmt, int
//                            width, int height);

/**
 * Setup the data pointers and linesizes based on the specified image
 * parameters and the provided array.
 *
 * The fields of the given image are filled in by using the src
 * address which points to the image data buffer. Depending on the
 * specified pixel format, one or multiple image data pointers and
 * line sizes will be set.  If a planar format is specified, several
 * pointers will be set pointing to the different picture planes and
 * the line sizes of the different planes will be stored in the
 * lines_sizes array. Call with src == NULL to get the required
 * size for the src buffer.
 *
 * To allocate the buffer and fill in the dst_data and dst_linesize in
 * one call, use av_image_alloc().
 *
 * @param dst_data      data pointers to be filled in
 * @param dst_linesize  linesizes for the image in dst_data to be filled in
 * @param src           buffer which will contain or contains the actual image
 * data, can be NULL
 * @param pix_fmt       the pixel format of the image
 * @param width         the width of the image in pixels
 * @param height        the height of the image in pixels
 * @param align         the value used in src for linesize alignment
 * @return the size in bytes required for src, a negative error code
 * in case of failure
 */
// int av_image_fill_arrays(uint8_t *dst_data[4], int dst_linesize[4],
//                          const uint8_t *src,
//                          enum AVPixelFormat pix_fmt, int width, int height,
//                          int align);

/**
 * Check if the given dimension of an image is valid, meaning that all
 * bytes of the image can be addressed with a signed int.
 *
 * @param w the width of the picture
 * @param h the height of the picture
 * @param log_offset the offset to sum to the log level for logging with log_ctx
 * @param log_ctx the parent logging context, it may be NULL
 * @return >= 0 if valid, a negative error code otherwise
 */
// int av_image_check_size(unsigned int w, unsigned int h, int log_offset, void
// *log_ctx);

/**
 * Check if the given dimension of an image is valid, meaning that all
 * bytes of a plane of an image with the specified pix_fmt can be addressed
 * with a signed int.
 *
 * @param w the width of the picture
 * @param h the height of the picture
 * @param max_pixels the maximum number of pixels the user wants to accept
 * @param pix_fmt the pixel format, can be AV_PIX_FMT_NONE if unknown.
 * @param log_offset the offset to sum to the log level for logging with log_ctx
 * @param log_ctx the parent logging context, it may be NULL
 * @return >= 0 if valid, a negative error code otherwise
 */
// int av_image_check_size2(unsigned int w, unsigned int h, int64_t max_pixels,
// enum AVPixelFormat pix_fmt, int log_offset, void *log_ctx);

/**
 * Check if the given sample aspect ratio of an image is valid.
 *
 * It is considered invalid if the denominator is 0 or if applying the ratio
 * to the image size would make the smaller dimension less than 1. If the
 * sar numerator is 0, it is considered unknown and will return as valid.
 *
 * @param w width of the image
 * @param h height of the image
 * @param sar sample aspect ratio of the image
 * @return 0 if valid, a negative AVERROR code otherwise
 */
// int av_image_check_sar(unsigned int w, unsigned int h, AVRational sar);

/**
 * Overwrite the image data with black. This is suitable for filling a
 * sub-rectangle of an image, meaning the padding between the right most pixel
 * and the left most pixel on the next line will not be overwritten. For some
 * formats, the image size might be rounded up due to inherent alignment.
 *
 * If the pixel format has alpha, the alpha is cleared to opaque.
 *
 * This can return an error if the pixel format is not supported. Normally, all
 * non-hwaccel pixel formats should be supported.
 *
 * Passing NULL for dst_data is allowed. Then the function returns whether the
 * operation would have succeeded. (It can return an error if the pix_fmt is
 * not supported.)
 *
 * @param dst_data      data pointers to destination image
 * @param dst_linesize  linesizes for the destination image
 * @param pix_fmt       the pixel format of the image
 * @param range         the color range of the image (important for colorspaces
 * such as YUV)
 * @param width         the width of the image in pixels
 * @param height        the height of the image in pixels
 * @return 0 if the image data was cleared, a negative AVERROR code otherwise
 */
// int av_image_fill_black(uint8_t *dst_data[4], const ptrdiff_t
// dst_linesize[4],
//                         enum AVPixelFormat pix_fmt, enum AVColorRange range,
//                         int width, int height);
} // namespace ffmpeg
