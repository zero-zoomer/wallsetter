fork of khonsaloh/wallsetter - fork of hsetroot - imlib2-based wallpaper changer

I've added simple motion. Still WIP, at the moment I am making kitty run in the fullscreen in the back with 0 opacity and with a blinking cursor to make the motion possible. Stil not sure how to fix this. I used blender to define the motion, in the provided blender file, run the script and coordinates will be written to a file called 'from_blender.txt'.

licence: GPL2  
needs: `libimlib2-dev, libx11-dev, libxinerama-dev, pkg-config, make`

after that:
```
sudo make install
```
or you can run only `make` and symlink or specify the whole path when calling it
