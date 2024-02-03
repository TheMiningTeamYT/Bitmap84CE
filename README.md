# Bitmap84CE
A project to attempt to display generic bitmap images on the TI 84 Plus CE
Now, a high quality image viewing application [(TheLastMillennial's HD-Picture-Viewer)](https://github.com/TheLastMillennial/HD-Picture-Viewer) already exists. However, it has some problems. 3 problems to my mind:
1: It uses 8bpp indexed mode. This was done, I presume, to make the images smaller and the code easier to write (as the excellent `GRAPHX` library features many useful tools for working in 8bpp indexed mode). However, the step of converting true color images to a 256 color palette is often lossy, resulting in artifacts and image degredation due to the reduced number of colors.
2: It requires images to be converted using a special utility. I'm sure that utility is fine (although I run Linux and so I haven't gotten to test it), if you can implement bitmap support, you'll suddenly be able to use nearly any image editor on the planet to convert your images. Instead of writing a utility yourself, you can lean on the decades of work that has gone into image manipulation utilities to make them stable, easy to use, compatible with a variety of source formats, and available on a dizzying variety of platforms, all while making the image conversion process easier to learn and more accessible. Anyone who knows how to open and save out a picture in MS paint will be able to convert an image for use on their calculator.
3: It requires the images to be stored on the calculator's archive. The TI 84 Plus CE has 4 MiB of archive space. The MSDDRVCE library supports devices up to 2 TiB. Allowing users to store images on USB MSDs will make the process of transferring images to the calculator faster, easier, and allow possibly half a million times more images to be stored.

Right now, the project is in Pre-Alpha. A lot of work will need to be done to get it to a finished state, but with any luck, we'll get there soon.
