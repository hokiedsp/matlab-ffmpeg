vr = ffmpeg.Reader('xylophone.mp4');
vr = ffmpeg.Reader('Downton Abbey Soundtrack (Full) (152kbit_Opus).ogg','FilterGraph','asplit [a][out1];[a] showspectrum=mode=separate:color=intensity:slide=1:scale=cbrt [out0]')
vr = ffmpeg.Reader('xylophone.mp4','FilterGraph','split [main][tmp]; [tmp] crop=iw:ih/2:0:0, vflip [flip]; [main][flip] overlay=0:H/2')
