SFLAGS = -V -q -g -e main

all: shaders

shaders: test.spv test2.spv sha256.spv ones.spv conv1.spv conv2.spv shuffle2.spv
	
test.spv: test.comp
	glslangValidator test.comp $(SFLAGS) -o test.spv -DCOPY

test2.spv: test.comp
	glslangValidator test.comp $(SFLAGS) -o test2.spv -DCOPYADD

sha256.spv: sha256.comp
	glslangValidator sha256.comp $(SFLAGS) -o sha256.spv -DMINING

ones.spv: conv123.comp
	glslangValidator conv123.comp $(SFLAGS) -o ones.spv -DONES

conv1.spv: conv123.comp
	glslangValidator conv123.comp $(SFLAGS) -o conv1.spv -DCONV1D

conv2.spv: conv123.comp
	glslangValidator conv123.comp $(SFLAGS) -o conv2.spv -DCONV2D

shuffle2.spv: conv123.comp
	glslangValidator conv123.comp $(SFLAGS) -o shuffle2.spv -DSHUFFLE2D

clean:
	rm -f *.spv
