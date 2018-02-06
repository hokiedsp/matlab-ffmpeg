#include "ffmpegStreamInput.h"

using namespace ffmpeg;

InputVideoStream::InputVideoStream(AVStream *st, IAVFrameSink *buf)
 : InputStream(st, buf)
{
  bparams.type = AVMEDIA_TYPE_VIDEO;
}
InputVideoStream::~InputVideoStream() {}

void InputVideoStream::open(AVStream *s)
{
  InputStream::open(s);
  if (st)
  {
    AVCodecParameters *par = st->codecpar;
    vparams = {par->width, par->height, par->sample_aspect_ratio, (AVPixelFormat)par->codec_type};
  }
}
void InputVideoStream::close()
{
  InputStream::close();
  vparams = {0, 0, {0, 0}, AV_PIX_FMT_NONE};
  bparams.type = AVMEDIA_TYPE_VIDEO;
}

AVRational InputVideoStream::getAvgFrameRate() const { return st ? st->avg_frame_rate : AVRational({0, 0}); }

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
