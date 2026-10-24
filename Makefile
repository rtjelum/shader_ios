ifeq ($(shell uname -s),Darwin)
GL_LIBS=-framework GLUT -framework OpenGL -framework Cocoa -lobjc
else
GL_LIBS=-lglut -lGLEW -lGL
endif

shadertoy: shadertoy.c
	$(CC) -Wall -Wextra -g -o $@ $< `pkg-config --libs --cflags gdk-pixbuf-2.0` $(GL_LIBS) -lm
