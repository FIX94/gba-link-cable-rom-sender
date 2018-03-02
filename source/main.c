/*
 * Copyright (C) 2018 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <gccore.h>
#include <stdio.h>
#include <malloc.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <fat.h>
#ifdef HW_RVL
#include <wiiuse/wpad.h>
#endif

//from my tests 50us seems to be the lowest
//safe si transfer delay in between calls
#define SI_TRANS_DELAY 50

extern u8 sidthekid_gba[];
extern u32 sidthekid_gba_size;

u8 *resbuf,*cmdbuf;

volatile u32 transval = 0;
void transcb(s32 chan, u32 ret)
{
	transval = 1;
}

volatile u32 resval = 0;
void acb(s32 res, u32 val)
{
	resval = val;
}

unsigned int docrc(u32 crc, u32 val)
{
	int i;
	for(i = 0; i < 0x20; i++)
	{
		if((crc^val)&1)
		{
			crc>>=1;
			crc^=0xa1c1;
		}
		else
			crc>>=1;
		val>>=1;
	}
	return crc;
}

static inline void wait_for_transfer()
{
	//350 is REALLY pushing it already, cant go further
	do{ usleep(350); }while(transval == 0);
}

unsigned int calckey(unsigned int size)
{
	unsigned int ret = 0;
	size=(size-0x200) >> 3;
	int res1 = (size&0x3F80) << 1;
	res1 |= (size&0x4000) << 2;
	res1 |= (size&0x7F);
	res1 |= 0x380000;
	int res2 = res1;
	res1 = res2 >> 0x10;
	int res3 = res2 >> 8;
	res3 += res1;
	res3 += res2;
	res3 <<= 24;
	res3 |= res2;
	res3 |= 0x80808080;

	if((res3&0x200) == 0)
	{
		ret |= (((res3)&0xFF)^0x4B)<<24;
		ret |= (((res3>>8)&0xFF)^0x61)<<16;
		ret |= (((res3>>16)&0xFF)^0x77)<<8;
		ret |= (((res3>>24)&0xFF)^0x61);
	}
	else
	{
		ret |= (((res3)&0xFF)^0x73)<<24;
		ret |= (((res3>>8)&0xFF)^0x65)<<16;
		ret |= (((res3>>16)&0xFF)^0x64)<<8;
		ret |= (((res3>>24)&0xFF)^0x6F);
	}
	return ret;
}
void doreset()
{
	cmdbuf[0] = 0xFF; //reset
	transval = 0;
	SI_Transfer(1,cmdbuf,1,resbuf,3,transcb,SI_TRANS_DELAY);
	while(transval == 0) ;
}
void getstatus()
{
	cmdbuf[0] = 0; //status
	transval = 0;
	SI_Transfer(1,cmdbuf,1,resbuf,3,transcb,SI_TRANS_DELAY);
	while(transval == 0) ;
}
u32 recv()
{
	memset(resbuf,0,32);
	cmdbuf[0]=0x14; //read
	transval = 0;
	SI_Transfer(1,cmdbuf,1,resbuf,5,transcb,SI_TRANS_DELAY);
	while(transval == 0) ;
	return *(vu32*)resbuf;
}
void send(u32 msg)
{
	cmdbuf[0]=0x15;cmdbuf[1]=(msg>>0)&0xFF;cmdbuf[2]=(msg>>8)&0xFF;
	cmdbuf[3]=(msg>>16)&0xFF;cmdbuf[4]=(msg>>24)&0xFF;
	transval = 0;
	resbuf[0] = 0;
	SI_Transfer(1,cmdbuf,5,resbuf,1,transcb,SI_TRANS_DELAY);
	while(transval == 0) ;
}
typedef struct _gbNames {
	char name[256];
} gbNames;

int compare (const void * a, const void * b ) {
  return strcmp((*(gbNames*)a).name, (*(gbNames*)b).name);
}

int main(int argc, char *argv[]) 
{
	void *xfb = NULL;
	GXRModeObj *rmode = NULL;
	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode(NULL);
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
	int x = 24, y = 32, w, h;
	w = rmode->fbWidth - (32);
	h = rmode->xfbHeight - (48);
	CON_InitEx(rmode, x, y, w, h);
	VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
	PAD_Init();
#ifdef HW_RVL
	WPAD_Init();
#endif
	cmdbuf = memalign(32,32);
	resbuf = memalign(32,32);
	u8 *gbaBuf = malloc(0x40000);
	fatInitDefault();
	int gbaCnt = 0;
	DIR *dir = opendir("/gba");
	struct dirent *dent;
	gbNames *names;
    if(dir!=NULL)
    {
        while((dent=readdir(dir))!=NULL)
		{
            if(strstr(dent->d_name,".gba") != NULL)
				gbaCnt++;
		}
		closedir(dir);
		names = malloc(sizeof(gbNames)*gbaCnt);
		memset(names,0,sizeof(gbNames)*gbaCnt);
		dir = opendir("/gba");
		int i = 0;
        while((dent=readdir(dir))!=NULL)
		{
            if(strstr(dent->d_name,".gba") != NULL)
			{
				strcpy(names[i].name,dent->d_name);
				i++;
			}
		}
		closedir(dir);
    }
	if(gbaCnt == 0)
	{
		printf("No Files! Make sure you have .gba files in your \"gba\" folder!\n");
		VIDEO_WaitVSync();
		VIDEO_WaitVSync();
		sleep(5);
		return 0;
	}
	qsort(names, gbaCnt, sizeof(gbNames), compare);
	int i;
	while(1)
	{
		i = 0;
		while(1)
		{
			printf("\x1b[2J");
			printf("\x1b[37m");
			printf("GBA Link Cable ROM Sender v1.0 by FIX94\n");
			printf("Select GBA ROM file\n");
			printf("<< %s >>\n",names[i].name);
			PAD_ScanPads();
			VIDEO_WaitVSync();
			u32 btns = PAD_ButtonsDown(0);
			#ifdef HW_RVL
			WPAD_ScanPads();
			u32 wbtns = WPAD_ButtonsDown(0);
			//merge wbtns into btns
			if((wbtns & WPAD_BUTTON_A) || (wbtns & WPAD_CLASSIC_BUTTON_A))
				btns |= PAD_BUTTON_A;
			if((wbtns & WPAD_BUTTON_RIGHT) || (wbtns & WPAD_CLASSIC_BUTTON_RIGHT))
				btns |= PAD_BUTTON_RIGHT;
			if((wbtns & WPAD_BUTTON_LEFT) || (wbtns & WPAD_CLASSIC_BUTTON_LEFT))
				btns |= PAD_BUTTON_LEFT;
			if((wbtns & WPAD_BUTTON_HOME) || (wbtns & WPAD_CLASSIC_BUTTON_HOME))
				btns |= PAD_BUTTON_START;
			#endif
			//handle selected option
			if(btns & PAD_BUTTON_A)
				break;
			else if(btns & PAD_BUTTON_RIGHT)
			{
				i++;
				if(i >= gbaCnt) i = 0;
			}
			else if(btns & PAD_BUTTON_LEFT)
			{
				i--;
				if(i < 0) i = (gbaCnt-1);
			}
			else if(btns & PAD_BUTTON_START)
			{
				printf("Exit...\n");
				VIDEO_WaitVSync();
				VIDEO_WaitVSync();
				sleep(5);
				return 0;
			}
		}
		char romF[256];
		sprintf(romF,"/gba/%s",names[i].name);
		FILE *f = fopen(romF,"rb");
		if(f == NULL) continue;
		fseek(f,0,SEEK_END);
		size_t gbaSize = ftell(f);
		rewind(f);
		if(gbaSize > 0x40000)
		{
			printf("ROM larger than 256KB!\n");
			VIDEO_WaitVSync();
			sleep(2);
			fclose(f);
			continue;
		}
		fread(gbaBuf,gbaSize,1,f);
		fclose(f);
		printf("Waiting for GBA in port 2...\n");
		resval = 0;

		SI_GetTypeAsync(1,acb);
		while(1)
		{
			if(resval)
			{
				if(resval == 0x80 || resval & 8)
				{
					resval = 0;
					SI_GetTypeAsync(1,acb);
				}
				else if(resval)
					break;
			}
		}
		if(resval & SI_GBA)
		{
			printf("GBA Found! Waiting for BIOS\n");
			resbuf[2]=0;
			while(!(resbuf[2]&0x10))
			{
				doreset();
				getstatus();
			}
			printf("GBA Ready, sending ROM File\n");
			unsigned int sendsize = (((gbaSize)+7)&~7);
			unsigned int ourkey = calckey(sendsize);
			//printf("Our Key: %08x\n", ourkey);
			//get current sessionkey
			u32 sessionkeyraw = recv();
			u32 sessionkey = __builtin_bswap32(sessionkeyraw^0x7365646F);
			//send over our own key
			send(__builtin_bswap32(ourkey));
			unsigned int fcrc = 0x15a0;
			//send over gba header
			for(i = 0; i < 0xC0; i+=4)
				send(__builtin_bswap32(*(vu32*)(gbaBuf+i)));
			//printf("Header done! Sending Goomba...\n");
			for(i = 0xC0; i < sendsize; i+=4)
			{
				u32 enc = ((gbaBuf[i+3]<<24)|(gbaBuf[i+2]<<16)|(gbaBuf[i+1]<<8)|(gbaBuf[i]));
				fcrc=docrc(fcrc,enc);
				sessionkey = (sessionkey*0x6177614B)+1;
				enc^=sessionkey;
				enc^=((~(i+(0x20<<20)))+1);
				enc^=0x20796220;
				send(enc);
			}
			fcrc |= (sendsize<<16);
			//printf("ROM done! CRC: %08x\n", fcrc);
			//send over CRC
			sessionkey = (sessionkey*0x6177614B)+1;
			fcrc^=sessionkey;
			fcrc^=((~(i+(0x20<<20)))+1);
			fcrc^=0x20796220;
			send(fcrc);
			//get crc back (unused)
			recv();
			printf("All done!\n");
			VIDEO_WaitVSync();
			VIDEO_WaitVSync();
			sleep(3);
		}
	}
	return 0;
}
