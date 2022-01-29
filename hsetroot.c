#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xinerama.h>
#include <Imlib2.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


typedef enum { Full, Fill, Center, Tile, Xtend, Cover } ImageMode;

void
usage(char *commandline)
{
  printf(
    "hsetroot - sets the wallpaper\n"
    "\n"
    "Syntax: %s [command1 [arg1..]] [command2 [arg1..]]..."
    "\n"
    "Generic Options:\n"
    " -root                      Treat multiple displays as one big screen (ignore xrandr outputs)\n"
    " -screens <int>             Set a screenmask to use\n"
    "\n"
    "Solid:\n"
    " -solid <color>             Render a solid using the specified color\n"
    "\n"
    "Image files:\n"
    " -center <image>            Render an image centered on screen\n"
    " -cover <image>             Render an image centered on screen scaled to fill the screen fully\n"
    " -tile <image>              Render an image tiled\n"
    " -full <image>              Render an image maximum aspect\n"
    " -extend <image>            Render an image max aspect and fill borders\n"
    " -fill <image>              Render an image stretched\n"
    "\n"
    "Misc:\n"
    " -alpha <amount>            Adjust alpha level for colors and images\n"
    "\n"
    "Colors are in the #rgb, #rrggbb, #rrggbbaa, rgb:1/2/3 formats or a X color name.\n"
    "\n"
  , commandline);
}

// Globals:
Display *display;
int screen;
int offset = 50;

// Adapted from fluxbox' bsetroot
int
setRootAtoms(Pixmap pixmap)
{
  Atom atom_root, atom_eroot, type;
  unsigned char *data_root, *data_eroot;
  int format;
  unsigned long length, after;

  atom_root = XInternAtom(display, "_XROOTMAP_ID", True);
  atom_eroot = XInternAtom(display, "ESETROOT_PMAP_ID", True);

  // doing this to clean up after old background
  if (atom_root != None && atom_eroot != None) {
    XGetWindowProperty(display, RootWindow(display, screen), atom_root, 0L, 1L, False, AnyPropertyType, &type, &format, &length, &after, &data_root);

    if (type == XA_PIXMAP) {
      XGetWindowProperty(display, RootWindow(display, screen), atom_eroot, 0L, 1L, False, AnyPropertyType, &type, &format, &length, &after, &data_eroot);

      if (data_root && data_eroot && type == XA_PIXMAP && *((Pixmap *) data_root) == *((Pixmap *) data_eroot))
        XKillClient(display, *((Pixmap *) data_root));
    }
  }

  atom_root = XInternAtom(display, "_XROOTPMAP_ID", False);
  atom_eroot = XInternAtom(display, "ESETROOT_PMAP_ID", False);

  if (atom_root == None || atom_eroot == None)
    return 0;

  // setting new background atoms
  XChangeProperty(display, RootWindow(display, screen), atom_root, XA_PIXMAP, 32, PropModeReplace, (unsigned char *) &pixmap, 1);
  XChangeProperty(display, RootWindow(display, screen), atom_eroot, XA_PIXMAP, 32, PropModeReplace, (unsigned char *) &pixmap, 1);

  return 1;
}

typedef struct {
  int r, g, b, a;
} Color;

int
parse_color(char *arg, Color *c, int default_alpha)
{
  Colormap colormap = DefaultColormap(display, screen);
  XColor color;

  c->a = default_alpha;

  // we support #rrggbbaa..
  if ((arg[0] == '#') && (strlen(arg) == 9)) {
    sscanf(arg + 7, "%2x", &(c->a));
    // ..but XParseColor wouldn't
    arg[7] = 0;
  }

  Status ret = XParseColor(display, colormap, arg, &color);
  if (ret == 0)
    return 0;

  c->r = color.red >> 8;
  c->g = color.green >> 8;
  c->b = color.blue >> 8;

  return 1;
}

int
load_image(ImageMode mode, const char *arg, int alpha, Imlib_Image rootimg, XineramaScreenInfo *outputs, int noutputs)
{
  int imgW, imgH, o;
  Imlib_Image buffer = imlib_load_image(arg);

  if (!buffer)
    return 0;

  imlib_context_set_image(buffer);
  imgW = imlib_image_get_width();
  imgH = imlib_image_get_height();

  if (alpha < 255) {
    // Create alpha-override mask
    imlib_image_set_has_alpha(1);
    Imlib_Color_Modifier modifier = imlib_create_color_modifier();
    imlib_context_set_color_modifier(modifier);

    DATA8 red[256], green[256], blue[256], alph[256];
    imlib_get_color_modifier_tables(red, green, blue, alph);
    for (o = 0; o < 256; o++)
      alph[o] = (DATA8) alpha;
    imlib_set_color_modifier_tables(red, green, blue, alph);

    imlib_apply_color_modifier();
    imlib_free_color_modifier();
  }

  imlib_context_set_image(rootimg);

  for (int i = 0; i < noutputs; i++) {
    XineramaScreenInfo o = outputs[i];
    o.width += offset;
    o.height += offset;
    printf("output %d: size(%d, %d) pos(%d, %d)\n", i, o.width, o.height, o.x_org, o.y_org);

    if (mode == Fill) {
      imlib_blend_image_onto_image(buffer, 0, 0, 0, imgW, imgH, o.x_org, o.y_org, o.width, o.height);
    } else if ((mode == Full) || (mode == Xtend) || (mode == Cover)) {
      double aspect = ((double) o.width) / imgW;
      if (((int) (imgH * aspect) > o.height) != /*xor*/ (mode == Cover))
        aspect = (double) o.height / (double) imgH;

      int top = (o.height - (int) (imgH * aspect)) / 2;
      int left = (o.width - (int) (imgW * aspect)) / 2;

      imlib_blend_image_onto_image(buffer, 0, 0, 0, imgW, imgH, o.x_org + left, o.y_org + top, (int) (imgW * aspect), (int) (imgH * aspect));

      if (mode == Xtend) {
        int w;

        if (left > 0) {
          int right = left - 1 + (int) (imgW * aspect);
          /* check only the right border - left is int divided so the right border is larger */
          for (w = 1; right + w < o.width; w <<= 1) {
            imlib_image_copy_rect(o.x_org + left + 1 - w, o.y_org, w, o.height, o.x_org + left + 1 - w - w, o.y_org);
            imlib_image_copy_rect(o.x_org + right, o.y_org, w, o.height, o.x_org + right + w, o.y_org);
          }
        }

        if (top > 0) {
          int bottom = top - 1 + (int) (imgH * aspect);
          for (w = 1; (bottom + w < o.height); w <<= 1) {
            imlib_image_copy_rect(o.x_org, o.y_org + top + 1 - w, o.width, w, o.x_org, o.y_org + top + 1 - w - w);
            imlib_image_copy_rect(o.x_org, o.y_org + bottom, o.width, w, o.x_org, o.y_org + bottom + w);
          }
        }
      }
    } else {  // Center || Tile
      int left = (o.width - imgW) / 2;
      int top = (o.height - imgH) / 2;

      if (mode == Tile) {
        int x, y;
        for (; left > 0; left -= imgW);
        for (; top > 0; top -= imgH);

        for (x = left; x < o.width; x += imgW)
          for (y = top; y < o.height; y += imgH)
            imlib_blend_image_onto_image(buffer, 0, 0, 0, imgW, imgH, o.x_org + x, o.y_org + y, imgW, imgH);
      } else {
        imlib_blend_image_onto_image(buffer, 0, 0, 0, imgW, imgH, o.x_org + left, o.y_org + top, imgW, imgH);
      }
    }
  }

  imlib_context_set_image(buffer);
  imlib_free_image();

  imlib_context_set_image(rootimg);

  return 1;
}

int
main(int argc, char **argv)
{
  Visual *vis;
  Colormap cm;
  Imlib_Image image;
  int width, height, depth, i, alpha;
  Pixmap pixmap;
  Imlib_Color_Modifier modifier = NULL;
  unsigned long screen_mask = ~0;
  int opt_root = false;

  /* global */ display = XOpenDisplay(NULL);

  if (!display) {
    fprintf(stderr, "Cannot open X display!\n");
    exit(123);
  }

  // global options
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-screens") == 0) {
      int intval;

      if ((++i) >= argc) {
        fprintf(stderr, "Missing value\n");
        continue;
      }

      if ((sscanf(argv[i], "%i", &intval) == 0) || (intval < 0)) {
        fprintf(stderr, "Bad value (%s)\n", argv[i]);
        continue;
      }

      screen_mask = intval;
      continue;
    }

    if (strcmp(argv[i], "-root") == 0) {
      opt_root = true;
      continue;
    }
  }

  int noutputs = 0;
  XineramaScreenInfo *outputs = NULL;

  XineramaScreenInfo fake = {
    .x_org = 0,
    .y_org = 0,
    .width = 0,
    .height = 0,
  };

  if (opt_root) {
    noutputs = 1;
    outputs = &fake;
  } else {
    outputs = XineramaQueryScreens(display, &noutputs);
  }

  for (/* global */ screen = 0; screen < ScreenCount(display); screen++) {
    if ((screen_mask & (1 << screen)) == 0)
      continue;

    Imlib_Context *context = imlib_context_new();
    imlib_context_push(context);

    imlib_context_set_display(display);
    vis = DefaultVisual(display, screen);
    cm = DefaultColormap(display, screen);
    width = DisplayWidth(display, screen) + offset;
    height = DisplayHeight(display, screen) + offset;
    int real_width = DisplayWidth(display, screen);
    int real_height = DisplayHeight(display, screen);
    depth = DefaultDepth(display, screen);

    if (opt_root) {
      outputs[0].width = width;
      outputs[0].height = height;
    }

    pixmap = XCreatePixmap(display, RootWindow(display, screen), width, height, depth);

    imlib_context_set_visual(vis);
    imlib_context_set_colormap(cm);
    imlib_context_set_drawable(pixmap);
    imlib_context_set_color_range(imlib_create_color_range());

    image = imlib_create_image(width, height);
    imlib_context_set_image(image);

    imlib_context_set_color(0, 0, 0, 255);
    imlib_image_fill_rectangle(0, 0, width, height);

    imlib_context_set_dither(1);
    imlib_context_set_blend(1);

    alpha = 255;

    for (i = 1; i < argc; i++) {
      if (modifier != NULL) {
        imlib_apply_color_modifier();
        imlib_free_color_modifier();
      }
      modifier = imlib_create_color_modifier();
      imlib_context_set_color_modifier(modifier);

      if (strcmp(argv[i], "-alpha") == 0) {
        if ((++i) >= argc) {
          fprintf (stderr, "Missing alpha\n");
          continue;
        }
        if (sscanf(argv[i], "%i", &alpha) == 0) {
          fprintf (stderr, "Bad alpha (%s)\n", argv[i]);
          continue;
        }
      } else if (strcmp(argv[i], "-solid") == 0) {
        Color c;
        if ((++i) >= argc) {
          fprintf(stderr, "Missing color\n");
          continue;
        }
        if (parse_color(argv[i], &c, alpha) == 0) {
          fprintf (stderr, "Bad color (%s)\n", argv[i]);
          continue;
        }
        imlib_context_set_color(c.r, c.g, c.b, c.a);
        imlib_image_fill_rectangle(0, 0, width, height);
      } else if (strcmp(argv[i], "-fill") == 0) {
        if ((++i) >= argc) {
          fprintf(stderr, "Missing image\n");
          continue;
        }
        if (load_image(Fill, argv[i], alpha, image, outputs, noutputs) == 0) {
          fprintf(stderr, "Bad image (%s)\n", argv[i]);
          continue;
        }
      } else if (strcmp(argv[i], "-full") == 0) {
        if ((++i) >= argc) {
          fprintf(stderr, "Missing image\n");
          continue;
        }
        if (load_image(Full, argv[i], alpha, image, outputs, noutputs) == 0) {
          fprintf(stderr, "Bad image (%s)\n", argv[i]);
          continue;
        }
      } else if (strcmp(argv[i], "-extend") == 0) {
        if ((++i) >= argc) {
          fprintf(stderr, "Missing image\n");
          continue;
        }
        if (load_image(Xtend, argv[i], alpha, image, outputs, noutputs) == 0) {
          fprintf(stderr, "Bad image (%s)\n", argv[i]);
          continue;
        }
      } else if (strcmp(argv[i], "-tile") == 0) {
        if ((++i) >= argc) {
          fprintf(stderr, "Missing image\n");
          continue;
        }
        if (load_image(Tile, argv[i], alpha, image, outputs, noutputs) == 0) {
          fprintf(stderr, "Bad image (%s)\n", argv[i]);
          continue;
        }
      } else if (strcmp(argv[i], "-center") == 0) {
        if ((++i) >= argc) {
          fprintf(stderr, "Missing image\n");
          continue;
        }
        if (load_image(Center, argv[i], alpha, image, outputs, noutputs) == 0) {
          fprintf (stderr, "Bad image (%s)\n", argv[i]);
          continue;
        }
      } else if (strcmp(argv[i], "-cover") == 0) {
        if ((++i) >= argc) {
          fprintf(stderr, "Missing image\n");
          continue;
        }
        if (load_image(Cover, argv[i], alpha, image, outputs, noutputs) == 0) {
          fprintf (stderr, "Bad image (%s)\n", argv[i]);
          continue;
        }
      } else if (strcmp(argv[i], "-root") == 0) {
        /* handled as global, just skipping here, no arg */
      } else if (strcmp(argv[i], "-screens") == 0) {
        /* handled as global, just skipping here, + arg */
        i++;
      } else {
        usage(argv[0]);
        imlib_free_image();
        imlib_free_color_range();
        if (modifier != NULL) {
          imlib_context_set_color_modifier(modifier);
          imlib_free_color_modifier();
          modifier = NULL;
        }
        XFreePixmap(display, pixmap);
        exit(1);
      }
    }

    if (modifier != NULL) {
      imlib_context_set_color_modifier(modifier);
      imlib_apply_color_modifier();
      imlib_free_color_modifier();
      modifier = NULL;
    }

    imlib_render_image_on_drawable(0, 0);
    imlib_free_image();
    imlib_free_color_range();

    if (setRootAtoms(pixmap) == 0)
      fprintf(stderr, "Couldn't create atoms...\n");

    XKillClient(display, AllTemporary);
    XSetCloseDownMode(display, RetainTemporary);

    XSetWindowBackgroundPixmap(display, RootWindow(display, screen), pixmap);

    Pixmap new_pixmap;
    new_pixmap = XCreatePixmap(display, RootWindow(display, screen), width, height, depth);
    XCopyArea(display, pixmap, new_pixmap, DefaultGC(display,screen),0,0,width,height,0,0);
    
    int frame_count = 300; 
    int xcoords[] = {10,9,9,9,9,8,8,8,8,7,7,7,7,7,7,6,6,6,6,6,6,6,6,6,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,6,6,6,6,6,6,6,6,6,7,7,7,7,7,7,7,8,8,8,8,8,9,9,9,9,9,10,10,10,10,11,11,11,11,12,12,12,13,13,13,13,14,14,14,15,15,15,16,16,16,17,17,17,18,18,19,19,19,20,20,20,21,21,22,22,22,23,23,24,24,24,25,25,26,26,26,27,27,28,28,28,29,29,30,30,31,31,31,32,32,33,33,33,34,34,35,35,35,36,36,36,37,37,38,38,38,39,39,39,40,40,40,41,41,41,41,42,42,42,43,43,43,43,43,44,44,44,44,45,45,45,45,45,45,46,46,46,46,46,46,46,46,46,46,46,46,46,46,46,46,46,46,46,46,46,46,46,46,45,45,45,45,45,45,44,44,44,44,43,43,43,43,42,42,42,41,41,41,40,40,40,39,39,39,38,38,38,37,37,36,36,36,35,35,34,34,33,33,33,32,32,31,31,30,30,29,29,28,28,28,27,27,26,26,25,25,24,24,23,23,23,22,22,21,21,20,20,19,19,19,18,18,17,17,16,16,16,15,15,15,14,14,13,13,13,12,12,12,11,11,11,10,10,10,10};

    int ycoords[] = {10,10,10,11,11,11,11,12,12,12,13,13,13,14,14,14,15,15,15,16,16,16,17,17,17,18,18,18,19,19,20,20,20,21,21,22,22,22,23,23,24,24,24,25,25,25,26,26,27,27,27,28,28,29,29,29,30,30,30,31,31,31,32,32,32,33,33,33,34,34,34,35,35,35,36,36,36,36,37,37,37,37,38,38,38,38,38,39,39,39,39,39,39,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,39,39,39,39,39,39,39,38,38,38,38,38,37,37,37,37,36,36,36,36,35,35,35,35,34,34,34,33,33,33,32,32,32,32,31,31,31,30,30,30,29,29,28,28,28,27,27,27,26,26,26,25,25,25,24,24,24,23,23,22,22,22,21,21,21,20,20,20,19,19,19,18,18,18,17,17,17,16,16,16,15,15,15,15,14,14,14,13,13,13,13,12,12,12,12,11,11,11,11,10,10,10,10,10,9,9,9,9,9,8,8,8,8,8,8,7,7,7,7,7,7,7,6,6,6,6,6,6,6,6,6,6,6,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,6,6,6,6,6,6,6,6,6,6,6,7,7,7,7,7,7,7,8,8,8,8,8,9,9,9,9,10,10};

    XSelectInput(display,RootWindow(display,screen),ExposureMask);
    XEvent event;

    while(1)
    {
        int i;
        for(i=0;i<frame_count;i++)
        {
            XCopyArea(display, new_pixmap, pixmap, DefaultGC(display,screen),xcoords[i], ycoords[i],real_width,real_height, 0, 0);
            XSync(display,0);
            usleep(19000);
            XNextEvent(display, &event);
        }
    }

    XClearWindow(display, RootWindow(display, screen));

    XFlush(display);
    XSync(display, False);

    imlib_context_pop();
    imlib_context_free(context);
  }

  if (outputs != NULL) {
    if (!opt_root) {
      XFree(outputs);
    }

    outputs = NULL;
    noutputs = 0;
  }

  return 0;
}
