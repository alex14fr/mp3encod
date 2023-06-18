#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <libgen.h>

struct bpb {
	uint16_t bytesPerSector;
	uint8_t sectorsPerCluster;
	uint16_t rsvdSectorCount;
	uint8_t nFAT;
	uint16_t rootDirEntries;
	uint16_t totalSectorCount16;
	uint8_t mediaType;
	uint16_t sectorsPerFAT;
	uint8_t unused[8];
	uint32_t totalSectorCount32;
} __attribute__((__packed__));

struct bpbo {
	uint32_t fat;
	uint32_t root;
	uint32_t data;
	uint32_t nclusters;
};

#define ATTR_READ_ONLY 0x1
#define ATTR_HIDDEN 0x2
#define ATTR_SYSTEM 0x4
#define ATTR_VOLUME_ID 0x8
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20

struct fatdirent {
	char name[11];
	uint8_t attrib;
	char unused[14];
	uint16_t fstClus;
	uint32_t size;
} __attribute__((__packed__));

void printBPB(struct bpb *p) {
	uint32_t scount=p->totalSectorCount32;
	if(scount==0)
		scount=p->totalSectorCount16;

	printf("%d bytes per sector\n\
%d sectors per cluster\n\
%d reserved sectors\n\
%d FATs\n\
%d entries in root directory\n\
%d sectors total\n\
%d sectors per FAT\n", p->bytesPerSector, p->sectorsPerCluster, p->rsvdSectorCount, p->nFAT, p->rootDirEntries, scount, p->sectorsPerFAT);
}

void readBPB(int fd, struct bpb *param, struct bpbo *offs) {
	if(lseek(fd, 0xB, SEEK_SET) < 0) {
		perror("lseek");
		exit(1);
	}
	if(read(fd, param, sizeof(struct bpb)) < sizeof(struct bpb)) {
		perror("read");
		exit(1);
	}
	printBPB(param);
	if(param->sectorsPerFAT == 0) {
		fprintf(stderr, "non-FAT16 filesystem\n");
		exit(1);
	}
	uint16_t rootDirSect=(param->rootDirEntries*32+param->bytesPerSector-1)/param->bytesPerSector;
	uint32_t scount=param->totalSectorCount32;
	if(scount==0)
		scount=param->totalSectorCount16;
	scount -= param->rsvdSectorCount + param->nFAT * param->sectorsPerFAT + rootDirSect;
	uint32_t nclusters=scount/param->sectorsPerCluster;
	printf("%d data clusters\n", nclusters);
	if(nclusters<4085 || nclusters>65524) {
		fprintf(stderr, "non-FAT16 filesystem\n");
		exit(1);
	}
	if(param->nFAT != 1) {
		fprintf(stderr, "unsupported number of FATs (!= 1)\n");
	//	exit(1);
	}
	offs->fat=param->bytesPerSector*param->rsvdSectorCount;
	offs->root=offs->fat+param->bytesPerSector*param->sectorsPerFAT*param->nFAT;
	offs->data=offs->root+param->bytesPerSector*rootDirSect;
	offs->nclusters=nclusters;
	printf("FAT region at offset 0x%x\n\
Root directory region at offset 0x%x\n\
Data region at offset 0x%x\n", offs->fat, offs->root, offs->data);
}

#define FAT_ENTRY_OFFS(offs, c) (offs->fat+2*c)
#define CLUSTER_OFFS(param, offs, c) (offs->data+param->bytesPerSector*param->sectorsPerCluster*(c-2))
#define ROOT_ENTRY_OFFS(offs, i) (offs->root+32*i)

uint16_t findConsecutiveFreeClusters(int fd, struct bpbo *offs, uint16_t nclusters) {
	uint16_t c, retclus;
	uint16_t curFreeClus=0;
	uint16_t x;
	lseek(fd, offs->fat+4, SEEK_SET);
	for(c=2; c<offs->nclusters-nclusters; c++) {
		if(read(fd, &x, 2) != 2) {
			perror("read");
			exit(1);
		}
		if(x != 0) {
			curFreeClus=0;
		} else {
			if(curFreeClus==0) retclus=c;
			curFreeClus++;
			if(curFreeClus == nclusters) return(retclus);
		}
	}
	return(0);
}

int16_t findConsecutiveFreeRootent(int fd, struct bpb *param, struct bpbo *offs, uint8_t nent) {
	int16_t i, reti;
	uint8_t curFreeEnt=0;
	uint8_t x;
	lseek(fd, offs->root, SEEK_SET);
	for(i=0; i<param->rootDirEntries-nent; i++) {
		if(read(fd, &x, 1) != 1) {
			perror("read");
			exit(1);
		}
		if(x != 0 && x!=0xe5) {
			curFreeEnt=0;
		} else {
			if(curFreeEnt==0) reti=i;
			curFreeEnt++;
			if(curFreeEnt == nent) return(reti);
		}
		lseek(fd, 31, SEEK_CUR);
	}
	return(-1);
}

char doschar(char c) {
	if((c>='0' && c<='9') || (c>='A' && c<='Z') || c=='_') return(c);
	else if(c>='a' && c<='z') return(c-'a'+'A');
	else return('_');
}

void dosName(char *unixName, char dosNam[11]) {
	int n=strlen(unixName);
	int i, j;
	for(i=0; i<11; i++) dosNam[i]=' ';
	for(i=0; i<n && unixName[i]!='.' && i<8; i++)
		dosNam[i]=doschar(unixName[i]);
	if(i==8) {
		for(; i<n && unixName[i]!='.'; i++);
	}
	if(i<n && unixName[i]=='.') {
		for(i++, j=8; j<11 && i<n && unixName[i]!='.'; j++, i++) {
			dosNam[j]=doschar(unixName[i]);
		}
	}
}

int main(int argc, char **argv) {
	if(argc==1) {
		printf("Usage: %s <fat16-file> <file1> ... <fileN>\n", argv[0]);	
		exit(0);
	}

	int fd=open(argv[1], O_RDWR);
	if(fd<0) {
		perror("open");
		exit(1);
	}
	struct bpb param;
	struct bpbo offs;
	readBPB(fd, &param, &offs);
	printf("\n");

	int nfiles=argc-2;
	int16_t rootent=findConsecutiveFreeRootent(fd, &param, &offs, nfiles);
	if(rootent == -1) {
		fprintf(stderr, "can not find suitable free root directory entry\n");
		exit(1);
	}

	uint8_t curf;
	for(curf=0; curf<nfiles; curf++) {
		struct stat sb;
		struct fatdirent de;
		memset(&de, 0, sizeof(struct fatdirent));
		uint16_t clus;
		if(stat(argv[curf+2], &sb)<0) {
			perror("stat");
			exit(1);
		}
		uint16_t clusSz=param.bytesPerSector*param.sectorsPerCluster;
		uint32_t szRnd=sb.st_size+clusSz-1;
		uint16_t szInClus=szRnd/(clusSz);
		de.size=sb.st_size;
		char *bname=basename(argv[curf+2]);
		char dosNam[11];
		dosName(bname, &(de.name[0]));
		fprintf(stderr, "%c%c%c%c%c%c%c%c.%c%c%c (%d clusters", 
				de.name[0],
				de.name[1],
				de.name[2],
				de.name[3],
				de.name[4],
				de.name[5],
				de.name[6],
				de.name[7],
				de.name[8],
				de.name[9],
				de.name[10],
				szInClus);
		clus=findConsecutiveFreeClusters(fd, &offs, szInClus);
		if(clus==0) {
			fprintf(stderr, ")\nnot enough consecutive free clusters\n");
			exit(1);
		}
		fprintf(stderr, ", starting at cluster %d, root directory entry #%d)\n", clus, rootent);
		de.fstClus=clus;
		int sfd=open(argv[curf+2], O_RDONLY);
		if(sfd<0) {
			perror("open");
			exit(1);
		}

		lseek(fd, ROOT_ENTRY_OFFS((&offs), rootent), SEEK_SET);
		write(fd, &de, sizeof(struct fatdirent));
		rootent++;

		lseek(fd, FAT_ENTRY_OFFS((&offs), clus), SEEK_SET);
		for(uint16_t c=clus; c<clus+szInClus-1; ) {
			uint16_t cpp=c+1;
			write(fd, &cpp, 2);
			c=cpp;
		}
		uint16_t cpp=0xffff;
		write(fd, &cpp, 2);

		lseek(fd, CLUSTER_OFFS((&param), (&offs), clus), SEEK_SET);
		char buf[4096];
		int nr, nread=0, nww, nw;
		while(nread<sb.st_size) {
			nr=read(sfd, buf, (sb.st_size-nread<4096 ? sb.st_size-nread : 4096));
			if(nr<=0) {
				perror("read");
				exit(1);
			}
			nread+=nr;
			nww=0;
			while(nww<nr) {
				nw=write(fd, buf+nww, nr-nww);
				if(nw<0) { 
					perror("write");
					exit(1);
				}
				nww+=nw;
			}
		}
	}
}

