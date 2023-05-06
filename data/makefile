SFLAGS = -V -q -g -e main

all: shaders

shaders: test.spv test2.spv sha256.spv pad1.spv conv1.spv unpad1.spv pad2.spv conv2.spv shuffle2.spv unpad2.spv
	
test.spv: test.comp
	glslangValidator test.comp $(SFLAGS) -o test.spv -DCOPY

test2.spv: test.comp
	glslangValidator test.comp $(SFLAGS) -o test2.spv -DCOPYADD

sha256.spv: sha256.comp
	glslangValidator sha256.comp $(SFLAGS) -o sha256.spv -DMINING

pad1.spv: conv123.comp
	glslangValidator conv123.comp $(SFLAGS) -o pad1.spv -DPAD1D

conv1.spv: conv123.comp
	glslangValidator conv123.comp $(SFLAGS) -o conv1.spv -DCONV1D

unpad1.spv: conv123.comp
	glslangValidator conv123.comp $(SFLAGS) -o unpad1.spv -DUNPAD1D

pad2.spv: conv123.comp
	glslangValidator conv123.comp $(SFLAGS) -o pad2.spv -DPAD2D

conv2.spv: conv123.comp
	glslangValidator conv123.comp $(SFLAGS) -o conv2.spv -DCONV2D

shuffle2.spv: conv123.comp
	glslangValidator conv123.comp $(SFLAGS) -o shuffle2.spv -DSHUFFLE2D

unpad2.spv: conv123.comp
	glslangValidator conv123.comp $(SFLAGS) -o unpad2.spv -DUNPAD2D

clean:
	rm -f *.spv
