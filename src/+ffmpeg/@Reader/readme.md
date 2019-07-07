C++ structure for mexReader.cpp

Three levels

* mexFFmpegReader in mexReader.h
* ffmpeg.Reader in ffmpeg/ffmpegReader.h
* ffmpeg.InputFormat in ffmpeg/ffmpegFormatInput.h
* ffmpeg.InputStream in ffmpeg/ffmpegStreamInput.h

Basic Operational Principles:
* *mexFFmpegReader* essentially is a MEX wrapper for ffmpeg.Reader
  * Its operation is to read one frame of the primary stream and how many ever frames available for the secondary streams.
  * The frames of the secondary streams are also consumed when user call the MATLAB "read" function. 
* *ffmpeg.Reader* performs all the buffering of the selected  media streams (streams of AVFrames)
  * It houses AVFrame queues for each streams which receive AVFrames either directly from the decoder or filter graph output. 
mexFFmpegReader 
