for of hsetroot - imlib2-based wallpaper changer

I removed a couple of functions and lines here and there to make it more minimal, just sets the wallpaper, not tries to change gamma, blur...
can accept images and solid colors as wallpaper. Thereis no massive change on the size of the final binary after compiling it but this is another opcion, nothing else.

licence: GPL2  
needs libimlib2-dev, libx11-dev, libxinerama-dev, pkg-config, make

after that:
```
sudo make install
```
or you can run only `Make` and symlink or specify the whole path when calling it
