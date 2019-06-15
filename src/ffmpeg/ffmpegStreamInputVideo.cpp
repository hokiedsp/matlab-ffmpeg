#include "ffmpegStreamInput.h"

using namespace ffmpeg;

// AVRational InputVideoStream::getSAR()
// {
//   return fmt_ctx ? av_guess_sample_aspect_ratio(fmt_ctx, st, firstframe) : AVRational({0, 1});
// }

// int InputVideoStream::getBitsPerPixel() const {}
// {
//   const AVPixFmtDescriptor *pix_desc = getPixFmtDescriptor();
//   return pix_desc?av_get_bits_per_pixel(pix_desc):NULL;
// }

// const AVPixFmtDescriptor *InputVideoStream::getPixFmtDescriptor() const { return ctx ? av_pix_fmt_desc_get(ctx->pix_fmt) : NULL; }
// size_t InputVideoStream::getNbPixelComponents() const { return getPixFmtDescriptor().nb_components; }
// size_t InputVideoStream::getWidth() const { return ctx ? width : 0; }
// size_t InputVideoStream::getHeight() const { return ctx ? height : 0; }
// size_t InputVideoStream::getFrameSize() const { return getWidth() * getHeight() * getNbPixelComponents(); }
