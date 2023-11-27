#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <lame/lame.h>

#define BYTES_PER_SAMP 4
#define BUFSAMP 6*1152*2*BYTES_PER_SAMP
#define BUFOUTSZ BUFSAMP
#define BYTES_PER_FILE 10*60*44100*BYTES_PER_SAMP
#define MIN(a,b) ( (a<b)?(a):(b) )

int main(int argc, char **argv) {
	int n_file=0;
	short int buf_in[BUFSAMP];
	unsigned char buf_out[BUFOUTSZ];
	int outsz;
	int finished=0;
	unsigned int bytesRead=0;
	char fnam[256];
	int fd;
	lame_global_flags *lamefl;

	if(argc<3) {
		printf("Usage: %s out%%02d.mp3 bitrate(kbps) [stereo] < [raw PCM, signed 16-bit LE 44100 hz stereo]\n", argv[0]);
		exit(1);
	}
	while(!finished) {
		snprintf(fnam, 256, argv[1], n_file);
		printf("Creating %s...\n", fnam);
		fd=open(fnam, O_WRONLY|O_CREAT|O_TRUNC, 0660);
		if(fd<0) {
			perror("open");
			exit(1);
		}
		bytesRead=0;
		lamefl=lame_init();
		//lame_set_out_samplerate(lamefl, 44100);
		lame_set_bWriteVbrTag(lamefl, 0);
		lame_set_quality(lamefl, 7);
		lame_set_disable_reservoir(lamefl, 1);
		lame_set_strict_ISO(lamefl, 1);
		//lame_set_num_channels(lamefl, 1);
		if(argc==3)
			lame_set_mode(lamefl, MONO); 
		lame_set_brate(lamefl, atoi(argv[2]));
		lame_init_params(lamefl);
		while(!finished && bytesRead<BYTES_PER_FILE) {
			size_t count=MIN(BYTES_PER_FILE-bytesRead, BUFSAMP);
			ssize_t nr = read(STDIN_FILENO, buf_in, count);
			if(nr == 0) {
				finished=1;
			} else if(nr < 0) {
				perror("read");
				exit(1);
			}
			bytesRead += nr;
			memset(buf_out, 0, BUFOUTSZ);
			for(uint32_t ii=0; ii<(count >> 1); ++ii) {
				if(buf_in[ii] > 16000) {
					buf_in[ii]=32760;
				} else if(buf_in[ii] < -16000) {
					buf_in[ii]=-32760;
				} else {
					buf_in[ii] *= 2; //buf_in[ii]/2;
				}
			}
			int nout=lame_encode_buffer_interleaved(lamefl, buf_in, nr/4, buf_out, BUFOUTSZ);
			write(fd, buf_out, nout);
		}
		int nout=lame_encode_flush(lamefl, buf_out, BUFOUTSZ);
		write(fd, buf_out, nout);
		lame_close(lamefl);
		n_file++;

	}


}
