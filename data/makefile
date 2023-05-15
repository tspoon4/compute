#SFLAGS = -V -q -g -e main
SFLAGS = -O -g -fentry-point=main

all: shaders

shaders: test.spv test2.spv sha256.spv pad1.spv conv1.spv unpad1.spv pad2.spv conv2.spv shuffle2.spv unpad2.spv pad3.spv conv3.spv unpad3.spv
	
test.spv: test.comp
	glslc test.comp $(SFLAGS) -o test.spv -DCOPY

test2.spv: test.comp
	glslc test.comp $(SFLAGS) -o test2.spv -DCOPYADD

sha256.spv: sha256.comp
	glslc sha256.comp $(SFLAGS) -o sha256.spv -DMINING

pad1.spv: conv123.comp
	glslc conv123.comp $(SFLAGS) -o pad1.spv -DPAD1D

conv1.spv: conv123.comp
	glslc conv123.comp $(SFLAGS) -o conv1.spv -DCONV1D

unpad1.spv: conv123.comp
	glslc conv123.comp $(SFLAGS) -o unpad1.spv -DUNPAD1D

pad2.spv: conv123.comp
	glslc conv123.comp $(SFLAGS) -o pad2.spv -DPAD2D

conv2.spv: conv123.comp
	glslc conv123.comp $(SFLAGS) -o conv2.spv -DCONV2D

shuffle2.spv: conv123.comp
	glslc conv123.comp $(SFLAGS) -o shuffle2.spv -DSHUFFLE2D

unpad2.spv: conv123.comp
	glslc conv123.comp $(SFLAGS) -o unpad2.spv -DUNPAD2D

pad3.spv: conv123.comp
	glslc conv123.comp $(SFLAGS) -o pad3.spv -DPAD3D

conv3.spv: conv123.comp
	glslc conv123.comp $(SFLAGS) -o conv3.spv -DCONV3D

unpad3.spv: conv123.comp
	glslc conv123.comp $(SFLAGS) -o unpad3.spv -DUNPAD3D

clean:
	rm -f *.spv
