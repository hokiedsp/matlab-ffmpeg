
// extern "C" {
// #include <libavutil/mem.h>
// // #include <libavformat/avformat.h>
// // #include <libavcodec/avcodec.h>
// }

// #include  <stdlib.h>

// #include <windows.h>
// #include <tchar.h>
// #include <stdio.h>

// #define BUFSIZE 4096

#include <string>
#include <mex.h>
#include <cstdlib>
#include <sstream>

#include "ffmpegInputFile.h"
#include "ffmpegOptionDefs.h"
#include "ffmpegOptionsContextInput.h"

void setFFmpegPath()
{
   mxArray *mval = NULL;

   if (mexCallMATLAB(1, &mval, 0, NULL, "ffmpegreadyformex") || !*mxGetLogicals(mval))
      mexErrMsgTxt("Either FFmpeg Toolbox is not properly installed or installed FFmpeg build does not have shared library files.");

   if (!mexCallMATLAB(1, &mval, 0, NULL, "ffmpegpath"))
   {
      char *str = mxArrayToString(mval);
      std::string ffmpeg_path(str);
      mxFree(str);

      // remove the file name
      std::string::size_type pos = ffmpeg_path.find_last_of("/\\");
      if (pos != std::string::npos)
         ffmpeg_path.erase(pos);

      // add to the environmental path
      std::string path(std::getenv("PATH"));
      if (path.find(ffmpeg_path) == std::string::npos) // false if already in the path
      {
         path += ";";
         path += ffmpeg_path;
         _putenv_s("PATH", path.c_str());
      }
   }
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
   // make sure ffmpeg folder is part of the PATH environment
   setFFmpegPath();

   ffmpeg::OptionDefs optdefs;
   ffmpeg::add_io_options(ffmpeg::add_in_options(optdefs));

   ffmpeg::InputOptionsContext opts(optdefs);
   ffmpeg::InputFile file("test.mp4", opts);

   // Check for existence.
   //std::experimental::filesystem::exists("helloworld.txt");

   //    if( experimental::filesystem::exists("code.cmd") )
   //       printf( "File exists\n");
   //    else
   //       printf( "File does not exist\n" );

   //    char full[_MAX_PATH];
   //    if( _fullpath( full, "code.cmd", _MAX_PATH ) != NULL )
   //       printf( "Full path is: %s\n", full );
   //    else
   //       printf( "Invalid path\n" );

   //     TCHAR  buffer[BUFSIZE]=TEXT("");
   //     TCHAR** lppPart={NULL};

   //     DWORD retval = GetFullPathName("code.cmd",
   //                  BUFSIZE,
   //                  buffer,
   //                  lppPart);

   //     if (retval == 0)
   //     {
   //         // Handle an error condition.
   //         printf ("GetFullPathName failed (%d)\n", GetLastError());
   //         return;
   //     }
   //     else
   //     {
   //         _tprintf(TEXT("The full path name is:  %s\n"), buffer);
   //         if (lppPart != NULL && *lppPart != 0)
   //         {
   //             _tprintf(TEXT("The final component in the path name is:  %s\n"), *lppPart);
   //         }
   //     }
}
