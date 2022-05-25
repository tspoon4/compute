SFLAGS = -V -q -g -e main

all: shaders

shaders: test.spv test2.spv sha256.spv
	
test.spv: test.comp
	glslangValidator test.comp $(SFLAGS) -o test.spv -DCOPY

test2.spv: test.comp
	glslangValidator test.comp $(SFLAGS) -o test2.spv -DCOPYADD

sha256.spv: sha256.comp
	glslangValidator sha256.comp $(SFLAGS) -o sha256.spv -DMINING

clean:
	rm -f *.spv
