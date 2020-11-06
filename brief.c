
#include <stdlib.h>
#include <string.h>

// include NESLIB header
#include "neslib.h"

// include CC65 NES Header (PPU)
#include <nes.h>

// APU (sound) support
#include "apu.h"
//#link "apu.c"

// link the pattern table into CHR ROM
//#link "chr_generic.s"

// BCD arithmetic support
#include "bcd.h"
//#link "bcd.c"

// VRAM update buffer
#include "vrambuf.h"
//#link "vrambuf.c"

// famitone2 library
//#link "famitone2.s"

// music and sfx
//#link "shooter.s"
extern char shooter_music_data[];

/// I'M DOING THE METASPRITE THING HERE

#define TILE 0xd8
#define ATTR1 1
#define ATTR 2
#define ENM1 0xf8
#define ENM2 0xfc
#define ENM3 0xc4


const unsigned char metasprite[]={
  	0,	0,	TILE+0,   ATTR1,
  	0,	8,	TILE+1,   ATTR1,
  	8,	0,	TILE+2,   ATTR1,
  	8,	8,	TILE+3,   ATTR1,
  128};

const unsigned char enemy1[]={
  	0,	0,	ENM1+0,   ATTR,
  	0,	8,	ENM1+1,   ATTR,
  	8,	0, 	ENM1+2,   ATTR,
  	8,	8,	ENM1+3,   ATTR,
128};

const unsigned char enemy2[]={
  	0,	0,	ENM2+0,   ATTR,
  	0,	8,	ENM2+1,   ATTR,
  	8,	0, 	ENM2+2,   ATTR,
  	8,	8,	ENM2+3,   ATTR,
128};

const unsigned char enemy3[]={
  	0,	0,	ENM3+0,   ATTR,
  	0,	8,	ENM3+1,   ATTR,
  	8,	0, 	ENM3+2,   ATTR,
  	8,	8,	ENM3+3,   ATTR,
128};



/*{pal:"nes",layout:"nes"}*/
const char PALETTE[32] = { 
  0x1C,			// screen color

  0x14,0x10,0x28,0x00,	// background palette 0
  0x35,0x20,0x2C,0x00,	// background palette 1
  0x00,0x10,0x20,0x00,	// background palette 2
  0x06,0x16,0x26,0x00,   // background palette 3

  0x16,0x35,0x24,0x00,	// sprite palette 0
  0x0D,0x10,0x25,0x00,	// sprite palette 1
  0x0D,0x37,0x2C,0x00,	// sprite palette 2
  0x0D,0x27,0x2A	// sprite palette 3
};




#define CHAR(x) ((x)-' ')
#define BLANK 0

void clrscr() {
  vrambuf_clear();
  ppu_off();
  vram_adr(NAMETABLE_A);
  vram_fill(BLANK, 32*28);
  vram_adr(0x0);
  ppu_on_all();
}





///////////////////////////////////////// Michael experiment
#define NSPRITES 8	// max number of sprites
#define NMISSILES 8	// max number of missiles
#define YOFFSCREEN 240	// offscreen y position (hidden)
#define XOFFSCREEN 240	// offscreen y position (hidden)


// sprite indexes
#define PLYRMISSILE 7	// player missile
#define PLYRSPRITE 7	// player sprite


#define VULCAN		0xD8
#define BADBOT		0xF8
#define MADBOT		0xC4
#define SADBOT		0xFC
#define MISSILE		0x1c

#define MAX_ATTACKERS 6

#define COLOR_PLAYER		1
#define ENEMY_COLOR		1
#define COLOR_FORMATION		1
#define COLOR_ATTACKER		1
#define COLOR_MISSILE		3
#define COLOR_BOMB		2
#define COLOR_SCORE		2
#define COLOR_EXPLOSION		3

typedef struct {
  byte alive;
  word x;
  word y;
  byte dx;
  byte dy;
  byte name;
  byte tag;
} AttackingEnemy;

typedef struct {
  byte xpos;
  byte ypos;
  signed char dx;
  signed char dy;
} Missile;

typedef struct {
  byte x;
  byte y;
  byte name;
  byte tag;
} Sprite;


AttackingEnemy attackers[MAX_ATTACKERS];
Missile missiles[NMISSILES];
Sprite vsprites[NSPRITES];

byte enemies;



void clrobjs() {
  byte i;
  memset(vsprites, 0, sizeof(vsprites));
  for (i=0; i<NSPRITES; i++) {
    vsprites[i].y = YOFFSCREEN;
  }
  for (i=0; i<NMISSILES; i++) {
    missiles[i].ypos = YOFFSCREEN;
  }
}

// Close mimic of the draw_bcd_word function in Solarian, instead scoring based on time.
void draw_bcd_word(word bcd)
{
  byte j;
  static char buffer[5];
  buffer[4] = CHAR('P');
  for (j=3; j<0x80; j--)
  {
    buffer[j] = CHAR('P' + (bcd & 0xf));
    bcd >>= 4;
  }
  vrambuf_put(NTADR_A(2, 4), buffer, 5);
}

// setup PPU and graphics function.
void setup_graphics() {
  // clear sprites
  oam_clear();
  // set palette colors
  pal_all(PALETTE);
  // Set up background
  vram_adr(0x0);
  //vram_write(TILESET, sizeof(TILESET));
  vrambuf_clear();
  set_vram_update(updbuf);
  // Turn on my PPU
  ppu_on_all();
}

// We check if the 
bool in_space(AttackingEnemy * a, byte mx, byte my)
{
  
  if( mx - a->x < (byte)16 && my - a->y < (byte)16)
  	return true;
  else
    	return false;
}

#define NUM_ACTORS 16
// Actor positions in x/y
byte actor_x[NUM_ACTORS];
byte actor_y[NUM_ACTORS];

// Deltas per-frame in horizontal and vertical movement
byte actor_dx[NUM_ACTORS];
byte actor_dy[NUM_ACTORS];

int overall_timer;
word total_score; // Total score needed to display on screen or whatever

// Make a function to change/add score according to current timer.
// Made to mimic the scoring system in Solarian, instead of using a given num, the current timer.
void timer_score(word num)
{
  total_score = bcd_add(total_score, num);
  draw_bcd_word(total_score);
}

//This is where sprites are put on screen
void copy_sprites() {
  byte i;
  byte oamid = 128; 
  for (i=0; i<NSPRITES; i++) {
    Sprite* spr = &vsprites[i];
    
    if (spr->y != YOFFSCREEN) {
      byte y = spr->y;
      byte x = spr->x;
      byte chr = spr->name;
      byte attr = spr->tag;
      
      if(spr->name == VULCAN){
      	oamid = oam_meta_spr(actor_x[0], actor_y[0], oamid, metasprite);
      }
      if(i > 0){
    	AttackingEnemy* a = &attackers[i];
        
    
      	if(spr->name == BADBOT && a->alive){
          
      	  oamid = oam_meta_spr(actor_x[i], actor_y[i], oamid, enemy1 );
      	}
      	if(spr->name == SADBOT && a->alive){
          
      	  oamid = oam_meta_spr(actor_x[i], actor_y[i], oamid, enemy2 );
      	}
      	if(spr->name == MADBOT && a->alive){
     
      	  oamid = oam_meta_spr(actor_x[i], actor_y[i], oamid, enemy3 );
      	}
      }
      else{
      	oamid = oam_spr(x, y, chr, attr, oamid);
      	oamid = oam_spr(x+8, y, chr^2, attr, oamid);
      }
    }
  }
  // copy all "shadow missiles" to video memory
  for (i=0; i<NMISSILES; i++) {
    Missile* mis = &missiles[i];
    if (mis->ypos != YOFFSCREEN) {
      oamid = oam_spr(mis->xpos, mis->ypos, MISSILE,
                      (i==7)?COLOR_MISSILE:COLOR_BOMB,
                      oamid);
    }
  }
  oam_hide_rest(oamid);
}

// CHANGES THE HORIZONTAL MOVEMENT OF PLAYER 1
void player_move() {
    char pad = pad_poll(0);
    
    if(pad&PAD_LEFT && actor_x[0]>0)
    actor_dx[0] = -2;
    else if(pad&PAD_RIGHT && actor_x[0]<240)
    actor_dx[0] = 2;
    else
    actor_dx[0] = 0;
    if ((pad & PAD_A) && missiles[PLYRMISSILE].ypos == YOFFSCREEN) {
    	missiles[PLYRMISSILE].ypos = actor_y[0]-8; // must be multiple of missile speed
    	missiles[PLYRMISSILE].xpos = actor_x[0]+4; // player X position
    	missiles[PLYRMISSILE].dy = -4; // player missile speed
  }
  vsprites[PLYRMISSILE].x = actor_x[0];
}

void move_missiles() { 
  byte i;
  for (i=0; i<8; i++) { 
    if (missiles[i].ypos != YOFFSCREEN) {
      // hit the bottom or top?
      if ((byte)(missiles[i].ypos += missiles[i].dy) > YOFFSCREEN) {
        missiles[i].ypos = YOFFSCREEN;
      }
    }
  }
}

// After missile detection, bullet disappears
void missile_hit() {
  missiles[PLYRMISSILE].ypos = YOFFSCREEN;
}

//checks if missile hits the enemies
void does_it_hit() {
  byte mx = missiles[PLYRMISSILE].xpos;
  byte my = missiles[PLYRMISSILE].ypos;
  byte i;
  if (missiles[PLYRMISSILE].ypos == YOFFSCREEN)
    return;
  for (i=1; i<MAX_ATTACKERS; i++) {
    AttackingEnemy* a = &attackers[i];
    if (a->alive && in_space(a, mx, my)) {
      a->alive = 0;
      enemies--;
      if(overall_timer > 5900)
      {
        timer_score(10);
      }
      else if (overall_timer > 5500)
      {
      	timer_score(9);
      }
      else if(overall_timer > 4200)
      {
      	timer_score(5); 
      }
      else if(overall_timer > 3000)
      {
      	timer_score(2); 
      }
      else if(overall_timer > 0)
      { 
        timer_score(1);
      }
      else
      {
        timer_score(0);
      }
      missile_hit();
      break;
    }
  }
}

//setup player data to be drawn in copy sprites
void draw_player() {
  vsprites[PLYRSPRITE].x = actor_x[0];
  vsprites[PLYRSPRITE].y = actor_y[0];
  vsprites[PLYRSPRITE].name = VULCAN;
  vsprites[PLYRSPRITE].tag = COLOR_PLAYER;
}

void new_player() {
  actor_x[0] = 120;
  draw_player();
}

//enemy setup stuff here
void enemy_onscreen(byte i) {
  AttackingEnemy* a = &attackers[i];
  
  if (a->alive == 0){
    return;
  }
  
  vsprites[i].x = actor_x[i];
  vsprites[i].y = actor_y[i];
  
  if(i % 3 == 1) vsprites[i].name = BADBOT;
  if(i % 3 == 2) vsprites[i].name = SADBOT;
  if(i % 3 == 0) vsprites[i].name = MADBOT;
 
  vsprites[i].tag = ENEMY_COLOR; 
}

void enemy_initial(byte i)
{
    AttackingEnemy* a = &attackers[i];
    a->alive = 1;
    a->x = 60 * i;
    a->y = 30 * i;
    
   
    actor_x[i] = 60 * i;
    actor_y[i] = 30 * i;
    actor_dx[i] = 2 * i;
    actor_dy[i] = 2 * i;
}

void enemy_setup(byte i)
{
    AttackingEnemy* a = &attackers[i];
    
    actor_x[i] +=  1;
    actor_y[i] = 30 * i;
    actor_dx[i] = 2 * i;
    actor_dy[i] = 2 * i;
  
    a->x = actor_x[i];
    a->y = 30 * i;
}



void draw_enemies() {
  byte i;
  for (i=1; i<MAX_ATTACKERS; i++) {
    AttackingEnemy* a = &attackers[i];
    if (a->alive == 1) {
      enemy_setup(i);
      enemy_onscreen(i);
    }
  }
}




void game_over()
{
  
  char pad;
  bool flag = false;
  byte j;
  int iter = 0;
  static char buffer[5];
  word bcd = total_score;
  
  ppu_off();
  oam_clear();
  vrambuf_clear();
  clrscr();
  
  
  vram_adr(NTADR_A(9, 12));
  vram_write("VULCAN CANNON", 13); 
  
  vram_adr(NTADR_A(10, 20));
  vram_write("PRESS START", 11); 
  ppu_on_all();
  
  for(iter = 0; iter < 50; iter++)
  {
    ppu_wait_frame();
  }
  
  while (!flag)
  {
    pad = pad_poll(0);
    if(pad&PAD_START)
    {
      flag = true;
      ppu_off();
      vram_adr(NTADR_A(9, 12));
      vram_write("            ", 13); 
      vram_adr(NTADR_A(10, 20));
      vram_write("          ", 11); 
    }
  }
  
 
  
  vram_adr(NTADR_A(3,13));
  vram_write("          ", 10);
  
  vram_adr(NTADR_A(3,13));
  vram_write("Game Over", 10); 
  
  vram_adr(NTADR_A(3,15));
  vram_write("Score: ", 7); 
  
  buffer[4] = CHAR('P');
  for (j=3; j<0x80; j--)
  {
    buffer[j] = CHAR('P' + (bcd & 0xf));
    bcd >>= 4;
  }
  vrambuf_put(NTADR_A(9, 15), buffer, 5);
  
  ppu_on_all();
  while(true)
  {
 
    
  }
}


//where the magic happens in game
void play_round() {
  register byte framecount;
  register byte t0;
  byte i;
  byte end_timer = 255;
  clrobjs();
  framecount = 0;
  new_player();
  timer_score(0);
  
  for(i=1; i < 4; i++){
    enemy_initial(i);
  }
  while (end_timer) {
    
    player_move();
    actor_x[0] += actor_dx[0];
    actor_y[0] += actor_dy[0];
    move_missiles();
    
    if(framecount & 1){
      does_it_hit();
    }
    
   
    draw_enemies();
    copy_sprites(); 
    
    overall_timer--;
    vrambuf_flush();
    
#ifdef DEBUG_FRAMERATE
    putchar(t0 & 31, 27, CHAR(' '));
    putchar(framecount & 31, 27, CHAR(' '));
#endif
    framecount++;
    t0 = nesclock();
#ifdef DEBUG_FRAMERATE
    putchar(t0 & 31, 27, CHAR('C'));
    putchar(framecount & 31, 27, CHAR('F'));
#endif
    if (enemies == 0)
        game_over();
  }
}

void title_screen(char pad, bool choice){
  int iter = 0;
  vram_adr(NTADR_A(9, 12));
  vram_write("VULCAN CANNON", 13); 
  
  vram_adr(NTADR_A(10, 20));
  vram_write("PRESS START", 11); 
  ppu_on_all();
  
  for(iter = 0; iter < 50; iter++)
  {
    ppu_wait_frame();
  }
  
  while (!choice)
  {
    pad = pad_poll(0);
    if(pad&PAD_START)
    {
      choice = true;
      ppu_off();
      vram_adr(NTADR_A(9, 12));
      vram_write("            ", 13); 
      vram_adr(NTADR_A(10, 20));
      vram_write("          ", 11); 
    }
  }
}


// set up famitone library
void setup_sounds() {
  famitone_init(shooter_music_data);
 // sfx_init(demo_sounds);
  nmi_set_callback(famitone_update);
}

void main()
{
  // Initilization of pad controller
  // and actor data, i and the oam_id
  char oam_id;
  char pad;
  bool flag = false;
  overall_timer = 6000;
  total_score = 0;
  setup_graphics();
  enemies = 3;

  
  // SET UP LOCATION FOR VULCAN
  actor_x[0] = 120;
  actor_y[0] = 200;
  actor_dx[0] = 0; 
  actor_dy[0] = 0;
  pad = 0; // PLAYER 1 ONLY SCREW TWO PLAYERS!!!!
  // enable rendering

  setup_sounds();
  ///////////////////////////////
  // PLAY YOUR GAME IDIOT
  ppu_on_all();
  title_screen(pad, flag);
  ppu_on_all();
  // draw message  
  vram_adr(NTADR_A(2,2));
  vram_write("Move left and right using \x1e\x1f", 28);
  // infinite loop
  while(1) {
    clrobjs();
    oam_id = 0;
    pad = pad_poll(0);
    music_play(0);		// start the music
    play_round();
    pal_all(PALETTE);
    oam_clear();
    oam_size(1); // 8x16 sprites
    clrscr();
    ppu_wait_frame();

  }

}









