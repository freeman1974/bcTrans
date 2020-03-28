#CROSS=
AR=$(CROSS)ar
CC=$(CROSS)gcc
CXX=$(CROSS)g++
STRIP=$(CROSS)strip
#x264|cuda
ENCODE=x264

DATE=$(shell date "+%Y%m%d-%H%M")

CFLAGS += -Wall -Wno-unused-but-set-variable -Wno-deprecated -Wno-unused-variable -DBNO=\"$(DATE)\" -DCODEC=\"$(ENCODE)\" -c -O2 -lpthread -Wno-unused-function
LDFLAGS =

INC+=-I./ -I./third/include 
ifeq ($(ENCODE), x264)
$(info -------compiling x264 version. encoder=libx264-------------)
# --disable-vaapi
#LIBS=-L./third/lib -lpthread -ldl -lm -lrt -lz -lbz2 -lva -lva-drm -lavfilter -lavformat -lavutil -lavcodec -lavcodec -lavfilter  -lavutil -lmp3lame -lspeex -lswresample  -lx264 -lavformat -lfdk-aac -lpostproc -lspeexdsp -lswscale -lyasm
LIBS=-L./third/lib -lpthread -ldl -lm -lrt -lz -lbz2 -lavfilter -lavformat -lavutil -lavcodec -lavcodec -lavfilter  -lavutil -lmp3lame -lspeex -lswresample  -lx264 -lavformat -lfdk-aac -lpostproc -lspeexdsp -lswscale -lyasm
else
$(info --------compiling cuda version. encoder=x264_nvenc---------)
#delete --enable-libnpp. default: we do not use cuda coz nvidia/cuda driver are NOT easily installed.
LIBS=-L./lib64_cuda -lpthread -ldl -lm -lrt -lz -lbz2 -lavfilter -lavformat -lavutil -lavcodec -lavcodec -lavfilter  -lavutil -lmp3lame -lspeex -lswresample  -lx264 -lavformat -lfdk-aac -lpostproc -lspeexdsp -lswscale -lyasm
endif

EXEC = bcTrans
OBJS = ffcrop.o utils.o cmdutils.o ffmpeg.o ffmpeg_cuvid.o ffmpeg_filter.o ffmpeg_opt.o ffmpeg_hw.o

.PHONY: all clean

all:$(EXEC)
$(EXEC): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LIBS)
	$(STRIP) $(EXEC)
	mv $@ ./bin/$@_$(ENCODE)

clean:
	rm -rf $(OBJS) $(EXEC) *.a

%.o: %.cpp
	$(CXX) -c $< $(INC)

%.o: %.c
	$(CC) -c $< ${CFLAGS} $(INC)

#%: %.cpp
#	$(CXX) -o $@ $< $(INC) $(LIBS)

#%: %.c
#	$(CC) -o $@ $< $(INC) $(LIBS)

