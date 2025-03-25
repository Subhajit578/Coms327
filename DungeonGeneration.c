#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __APPLE__
  #include <libkern/OSByteOrder.h>
  #define be32toh(x) OSSwapBigToHostInt32(x)
  #define htobe32(x) OSSwapHostToBigInt32(x)
  #define be16toh(x) OSSwapBigToHostInt16(x)
  #define htobe16(x) OSSwapHostToBigInt16(x)
#else
  #include <endian.h>
#endif

// ------ NCURSES includes ------
#include <curses.h>

// ------ DEFINES ------
#define DUNGEON_DIR   "/.rlg327/"
#define DUNGEON_FILE  "dungeon"
#define FILE_MARKER   "RLG327-S2025"
#define MARKER_LEN    12
#define FILE_VERSION  0
#define WIDTH         80
#define HEIGHT        21
#define MAX_ROOMS     10
#define DEFAULT_NUMMON 10  // Default monster count if --nummon not specified

// Global to store the user-specified monster count so that
// stairs (new_level) can honor that count instead of always using DEFAULT_NUMMON.
static int global_num_monsters = DEFAULT_NUMMON;

int upCount = 0, downCount = 0;
int up_xCoord = 0, down_xCoord = 0;
int up_yCoord = 0, down_yCoord = 0;

int room_x[MAX_ROOMS], room_y[MAX_ROOMS];
int room_w[MAX_ROOMS], room_h[MAX_ROOMS];
int room_count = 0;

char dungeon[HEIGHT][WIDTH];
int hardness[HEIGHT][WIDTH];
static char base_map[HEIGHT][WIDTH];

int pc_x = 0, pc_y = 0;

int disTunneling[HEIGHT][WIDTH];
int disNonTunneling[HEIGHT][WIDTH];

typedef struct {
  int x, y;
  int dist;
} node_t;

typedef struct {
  node_t *array;
  int size;
} heap_t;

typedef enum {
  char_pc,
  char_monster
} char_type_t;

typedef struct {
  char_type_t type;
  int alive;
  int x, y;
  int speed;
  int turn;
  int hp;
  int monster_btype;  // bit flags
  char symbol;
} character_t;

typedef struct {
  int time;
  character_t *c;
} event_t;

typedef struct {
  event_t *array;
  int size;
  int capacity;
} event_queue_t;

// ------ GLOBAL CHARACTER ARRAY ------
#define MAX_CHARACTERS 1000
static character_t characters[MAX_CHARACTERS];
static int num_characters = 0;
static int pc_is_alive = 1;

void load_dungeon(const char *path);
void save_dungeon(const char *path);
static int inBounds(int x, int y);
static void placePC(int x, int y);

void djikstraForTunnel(int sx,int sy);
void djikstraForNonTunnel(int sx,int sy);

void initializeDungeon();
void generateRooms();
void connectRoomsViaCorridor();
void placeStairs();

static void init_event_queue(event_queue_t*eq,int cap);
static void del_event_queue(event_queue_t*eq);
static void eq_push(event_queue_t*eq,event_t e);
static event_t eq_pop(event_queue_t*eq);
static int eq_empty(event_queue_t*eq);

// ncurses UI
static void init_curses();
static void end_curses();
static void display_dungeon();
static void display_message(const char *msg);
static void display_monster_list();
static void handle_pc_input(character_t *pc);
static void new_level(int nummon);

// monster and PC creation
static void create_monster();
static void create_pc();

// movement
static void do_monster_movement(character_t*m);

// utility
void checkDir();
void getPath(char*buf,size_t size);

void swap(node_t *a, node_t *b) {
  node_t temp = *a;
  *a = *b;
  *b = temp;
}
static int parentIdx(int i){ return (i-1)/2; }
static int leftIdx(int i){ return (2*i+1); }
static int rightIdx(int i){ return (2*i+2); }

void initializeHeap(heap_t*h,int capacity){
  h->array = malloc(capacity * sizeof(*h->array));
  h->size  = 0;
}
void deleteHeap(heap_t*h){
  free(h->array);
  h->array = NULL;
  h->size  = 0;
}
static void heapifyUp(heap_t*h,int i){
  while(i>0){
    int p = parentIdx(i);
    if(h->array[i].dist < h->array[p].dist){
      swap(&h->array[i], &h->array[p]);
      i = p;
    } else break;
  }
}
static void heapifyDown(heap_t*h,int i){
  while(1){
    int l = leftIdx(i), r = rightIdx(i), min = i;
    if(l < h->size && h->array[l].dist < h->array[min].dist) min = l;
    if(r < h->size && h->array[r].dist < h->array[min].dist) min = r;
    if(min == i) break;
    swap(&h->array[i], &h->array[min]);
    i = min;
  }
}
void insertNode(heap_t*h,node_t n){
  h->array[h->size] = n;
  h->size++;
  heapifyUp(h, h->size-1);
}
node_t deleteFromHeap(heap_t*h){
  node_t top = h->array[0];
  h->array[0] = h->array[h->size - 1];
  h->size--;
  heapifyDown(h,0);
  return top;
}

void djikstraForTunnel(int sx,int sy){
  for(int r=0;r<HEIGHT;r++){
    for(int c=0;c<WIDTH;c++){
      disTunneling[r][c] = INT32_MAX;
    }
  }
  disTunneling[sy][sx] = 0;

  heap_t h;
  initializeHeap(&h, WIDTH*HEIGHT);
  node_t start = { sx, sy, 0 };
  insertNode(&h, start);

  int dirs[8][2] = { {-1,0},{1,0},{0,-1},{0,1},{-1,-1},{-1,1},{1,-1},{1,1} };
  while(h.size > 0){
    node_t u = deleteFromHeap(&h);
    if(u.dist > disTunneling[u.y][u.x]) continue;

    for(int i=0; i<8; i++){
      int nx = u.x + dirs[i][0];
      int ny = u.y + dirs[i][1];
      if(!inBounds(nx,ny)) continue;
      if(hardness[ny][nx] != 255){
        int cost=1;
        if(hardness[ny][nx]>0 && hardness[ny][nx]<255){
          cost += hardness[ny][nx]/85;
        }
        int alt=u.dist + cost;
        if(alt < disTunneling[ny][nx]){
          disTunneling[ny][nx] = alt;
          node_t v = { nx, ny, alt };
          insertNode(&h,v);
        }
      }
    }
  }
  deleteHeap(&h);
}

void djikstraForNonTunnel(int sx,int sy){
  for(int r=0;r<HEIGHT;r++){
    for(int c=0;c<WIDTH;c++){
      disNonTunneling[r][c] = INT32_MAX;
    }
  }
  disNonTunneling[sy][sx] = 0;

  heap_t h; 
  initializeHeap(&h, WIDTH*HEIGHT);
  node_t start = { sx, sy, 0 };
  insertNode(&h, start);

  int dirs[8][2] = { {-1,0},{1,0},{0,-1},{0,1},{-1,-1},{-1,1},{1,-1},{1,1} };
  while(h.size>0){
    node_t u = deleteFromHeap(&h);
    if(u.dist > disNonTunneling[u.y][u.x]) continue;

    for(int i=0;i<8;i++){
      int nx = u.x + dirs[i][0];
      int ny = u.y + dirs[i][1];
      if(!inBounds(nx,ny)) continue;
      if(hardness[ny][nx]==0){
        int alt = u.dist + 1;
        if(alt < disNonTunneling[ny][nx]){
          disNonTunneling[ny][nx] = alt;
          node_t v = { nx, ny, alt };
          insertNode(&h,v);
        }
      }
    }
  }
  deleteHeap(&h);
}


static int inBounds(int x, int y) {
  return (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT);
}

void initializeDungeon(){
  for(int y=0;y<HEIGHT;y++){
    for(int x=0;x<WIDTH;x++){
      if(x==0 || x==WIDTH-1 || y==0 || y==HEIGHT-1){
        dungeon[y][x] = ' ';
        hardness[y][x] = 255;
      } else {
        dungeon[y][x] = ' ';
        hardness[y][x] = (rand()%254)+1;
      }
    }
  }
}

static void fillRoom(int w,int h,int x,int y){
  for(int row=y; row<y+h; row++){
    for(int col=x; col<x+w; col++){
      dungeon[row][col]='.';
      hardness[row][col]=0;
    }
  }
}

static int isValidRoom(int rw,int rh,int rx,int ry){
  if(rw<1 || rh<1 || (rw+rx>=WIDTH-1) || (rh+ry>=HEIGHT-1)) {
    return 0;
  }
  for(int row=ry; row<ry+rh; row++){
    for(int col=rx; col<rx+rw; col++){
      if(dungeon[row][col] != ' '){
        return 0;
      }
    }
  }
  return 1;
}

void generateRooms(){
  int attempts=2000;
  int c=0;
  while(attempts>0 && c<6){
    int rw=(rand()%6)+4;
    int rh=(rand()%4)+3;
    int rx=(rand()%(WIDTH-rw-2))+1;
    int ry=(rand()%(HEIGHT-rh-2))+1;
    if(isValidRoom(rw,rh,rx,ry)){
      fillRoom(rw,rh,rx,ry);
      room_x[c]=rx; room_y[c]=ry;
      room_w[c]=rw; room_h[c]=rh;
      c++;
    }
    attempts--;
  }
  room_count=c;
}

void connectRoomsViaCorridor(){
  if(room_count<2)return;
  for(int i=1;i<room_count;i++){
    int x1=room_x[i-1]+room_w[i-1]/2;
    int y1=room_y[i-1]+room_h[i-1]/2;
    int x2=room_x[i]+room_w[i]/2;
    int y2=room_y[i]+room_h[i]/2;
    while(x1!=x2){
      if(x1>=0&&x1<WIDTH&&y1>=0&&y1<HEIGHT){
        if(dungeon[y1][x1]!='.'){
          dungeon[y1][x1]='#';
          hardness[y1][x1]=0;
        }
      }
      x1 += (x2>x1) ? 1 : -1;
    }
    while(y1!=y2){
      if(x1>=0&&x1<WIDTH&&y1>=0&&y1<HEIGHT){
        if(dungeon[y1][x1]!='.'){
          dungeon[y1][x1]='#';
          hardness[y1][x1]=0;
        }
      }
      y1 += (y2>y1) ? 1 : -1;
    }
  }
}

void placeStairs(){
  int flag=1, upFlag=0, downFlag=0;
  while(flag){
    int up_x=rand()%WIDTH, up_y=rand()%HEIGHT;
    int down_x=rand()%WIDTH, down_y=rand()%HEIGHT;
    if((dungeon[up_y][up_x]=='.' || dungeon[up_y][up_x]=='#') && !upFlag){
      dungeon[up_y][up_x] = '<';
      up_xCoord=up_x; up_yCoord=up_y;
      upCount=1; upFlag=1;
    }
    if((dungeon[down_y][down_x]=='.' || dungeon[down_y][down_x]=='#')
       && !(up_x==down_x && up_y==down_y) && !downFlag){
      dungeon[down_y][down_x] = '>';
      down_xCoord=down_x; down_yCoord=down_y;
      downCount=1; downFlag=1;
    }
    if(upFlag && downFlag) flag=0;
  }
}


static void new_level(int nummon) {
  num_characters=0;
  initializeDungeon();
  generateRooms();
  connectRoomsViaCorridor();
  placeStairs();

  if(room_count>0) {
    pc_x = room_x[0];
    pc_y = room_y[0];
  } else {
    pc_x = 1;
    pc_y = 1;
  }

  memcpy(base_map, dungeon, sizeof(base_map));

  // place PC in the map array
  placePC(pc_x, pc_y);

  djikstraForNonTunnel(pc_x, pc_y);
  djikstraForTunnel(pc_x, pc_y);

  create_pc();
  for(int i=0; i<nummon; i++) {
    create_monster();
  }
}

// ----------------------------------------------------------------
// ------------------ PC & Monster Creation ------------------------
// ----------------------------------------------------------------
static void create_monster(){
  int rx,ry;
  do{
    rx=rand()%WIDTH;
    ry=rand()%HEIGHT;
  } while(dungeon[ry][rx] != '.'); 

  int flags = rand() & 0x0F; 
  int spd   = (rand()%16)+5;
  character_t*m = &characters[num_characters++];
  m->type = char_monster;
  m->alive=1;
  m->x=rx; m->y=ry;
  m->speed=spd; m->turn=0; m->hp=10;
  m->monster_btype=flags;
  char hex_map[]="0123456789abcdef";
  m->symbol=hex_map[flags];
  dungeon[ry][rx] = m->symbol;
}

static void create_pc(){
  character_t*pc=&characters[num_characters++];
  pc->type=char_pc; pc->alive=1;
  pc->x=pc_x; pc->y=pc_y;
  pc->speed=10; pc->turn=0; pc->hp=50;
  pc->monster_btype=0;
  pc->symbol='@';
  dungeon[pc_y][pc_x] = '@';
}

// ----------------------------------------------------------------
// --------------------- Movement & Attacks ------------------------
// ----------------------------------------------------------------
static void do_monster_movement(character_t*m)
{
  if(!m->alive) return;

  int oldx=m->x, oldy=m->y;

  int intelligence=(m->monster_btype&0x1)?1:0;
  int tunneling=(m->monster_btype&0x4)?1:0;
  int erratic=(m->monster_btype&0x8)?1:0;

  int do_random=0;
  if(erratic && (rand()%2==0)){
    do_random=1;
  }

  int bestx=m->x, besty=m->y;
  if(do_random){
    int rr=rand()%9;
    int ddx[9]={0,-1,1,0,0,-1,-1,1,1};
    int ddy[9]={0,0,0,-1,1,-1,1,-1,1};
    bestx=m->x+ddx[rr];
    besty=m->y+ddy[rr];
  } else if(!intelligence){
    int dx=(pc_x>m->x)?1:((pc_x<m->x)?-1:0);
    int dy=(pc_y>m->y)?1:((pc_y<m->y)?-1:0);
    bestx=m->x+dx; besty=m->y+dy;
  } else {
    int bestDist=INT32_MAX;
    for(int i=-1;i<=1;i++){
      for(int j=-1;j<=1;j++){
        if(!(i==0&&j==0)){
          int nx=m->x+j, ny=m->y+i;
          if(inBounds(nx,ny)){
            int d = tunneling ? disTunneling[ny][nx] : disNonTunneling[ny][nx];
            if(d < bestDist) {
              bestDist=d; bestx=nx; besty=ny;
            }
          }
        }
      }
    }
  }

  if(tunneling && hardness[besty][bestx]>0 && hardness[besty][bestx]<255){
    hardness[besty][bestx] -= 85;
    if(hardness[besty][bestx]>0) {
      return;
    }
    dungeon[besty][bestx]='#';
  }

  if(dungeon[besty][bestx]=='@') {
    pc_is_alive=0;
  }

  dungeon[oldy][oldx] = base_map[oldy][oldx];

  m->x=bestx; m->y=besty;
  if(m->alive) {
    dungeon[besty][bestx] = m->symbol;
  }
}

// ----------------------------------------------------------------
// ------------------------ Event Queue ----------------------------
// ----------------------------------------------------------------
static void swap_events(event_t*a,event_t*b){
  event_t tmp=*a;*a=*b;*b=tmp;
}
static int parentE(int i){return (i-1)/2;}
static int leftE(int i){return (2*i+1);}
static int rightE(int i){return (2*i+2);}

static void eq_heapify_up(event_queue_t*eq,int i){
  while(i>0){
    int p=parentE(i);
    if(eq->array[i].time<eq->array[p].time){
      swap_events(&eq->array[i],&eq->array[p]);
      i=p;
    } else break;
  }
}
static void eq_heapify_down(event_queue_t*eq,int i){
  while(1){
    int l=leftE(i),r=rightE(i),min=i;
    if(l<eq->size && eq->array[l].time<eq->array[min].time)min=l;
    if(r<eq->size && eq->array[r].time<eq->array[min].time)min=r;
    if(min==i)break;
    swap_events(&eq->array[i],&eq->array[min]);
    i=min;
  }
}
static void eq_push(event_queue_t*eq,event_t e){
  eq->array[eq->size]=e;
  eq->size++;
  eq_heapify_up(eq,eq->size-1);
}
static event_t eq_pop(event_queue_t*eq){
  event_t top=eq->array[0];
  eq->array[0]=eq->array[eq->size-1];
  eq->size--;
  eq_heapify_down(eq,0);
  return top;
}
static int eq_empty(event_queue_t*eq){
  return(eq->size==0);
}
static void init_event_queue(event_queue_t*eq,int cap){
  eq->array=malloc(cap*sizeof(*eq->array));
  eq->size=0; eq->capacity=cap;
}
static void del_event_queue(event_queue_t*eq){
  free(eq->array);
  eq->array=NULL; eq->size=0; eq->capacity=0;
}

// ----------------------------------------------------------------
// --------------------- Ncurses UI Code ---------------------------
// ----------------------------------------------------------------
static void init_curses() {
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  start_color();
}

static void end_curses() {
  endwin();
}

// Display a single-line message at the top (line 0).
static void display_message(const char *msg) {
  move(0,0);
  clrtoeol();
  mvprintw(0,0,"%s", msg);
  refresh();
}

// Print the entire dungeon using ncurses on lines 1..21.
static void display_dungeon() {
  for(int r=0; r<HEIGHT; r++){
    move(r+1, 0);
    for(int c=0; c<WIDTH; c++){
      addch(dungeon[r][c]);
    }
  }
  refresh();
}

// The 'm' command: show monster list.
static void display_monster_list()
{
  typedef struct {
    char symbol;
    int rel_x, rel_y;
  } moninfo_t;
  moninfo_t *list = malloc(num_characters*sizeof(*list));
  int list_size=0;
  for(int i=0; i<num_characters; i++){
    if(!characters[i].alive) continue;
    if(characters[i].type==char_pc) continue;
    list[list_size].symbol=characters[i].symbol;
    list[list_size].rel_x = characters[i].x - pc_x;
    list[list_size].rel_y = characters[i].y - pc_y;
    list_size++;
  }

  clear();
  refresh();

  int offset=0;
  const int lines_avail=20;
  int done=0;

  while(!done) {
    clear();
    mvprintw(0,0,"--- Monster List (press ESC to exit, up/down to scroll) ---");
    int line=1;
    for(int i=0;i<lines_avail;i++){
      int idx=offset+i;
      if(idx>=list_size)break;

      int dx=list[idx].rel_x;
      int dy=list[idx].rel_y;
      const char *vert=NULL,*horiz=NULL;
      int ady=(dy<0)?-dy:dy;
      int adx=(dx<0)?-dx:dx;

      if(dy<0)vert="north";
      else if(dy>0)vert="south";
      if(dx<0)horiz="west";
      else if(dx>0)horiz="east";

      char desc[80];
      if(vert&&horiz){
        snprintf(desc,sizeof(desc),"%c, %d %s and %d %s",
                 list[idx].symbol,ady,vert,adx,horiz);
      } else if(vert){
        snprintf(desc,sizeof(desc),"%c, %d %s",list[idx].symbol,ady,vert);
      } else if(horiz){
        snprintf(desc,sizeof(desc),"%c, %d %s",list[idx].symbol,adx,horiz);
      } else {
        snprintf(desc,sizeof(desc),"%c, same cell??",list[idx].symbol);
      }

      mvprintw(line++,0,"%s",desc);
    }
    refresh();

    int ch=getch();
    switch(ch){
      case 27:  
        done=1;break;
      case KEY_UP:
        if(offset>0)offset--;
        break;
      case KEY_DOWN:
        if(offset+lines_avail<list_size)offset++;
        break;
      default:
        break;
    }
  }
  free(list);
  clear();
  display_dungeon();
  display_message("Exited monster list.");
}

// The fix: check base_map instead of dungeon when pressing stairs
static int pc_can_walk_on(char cell)
{
  return (cell == '.' || cell == '#' || cell == '<' || cell == '>');
}

// Called when it’s the PC’s turn.  
static void handle_pc_input(character_t *pc)
{
  while(1) {
    int ch = getch();
    switch(ch) {
      case '7': case 'y': {
        int nx=pc->x-1; int ny=pc->y-1;
        if(inBounds(nx,ny) && pc_can_walk_on(dungeon[ny][nx])) {
          for(int i=0;i<num_characters;i++){
            if(characters[i].alive && characters[i].x==nx && characters[i].y==ny && &characters[i]!=pc){
              characters[i].alive=0;
              if(characters[i].type==char_pc) pc_is_alive=0;
            }
          }
          dungeon[pc->y][pc->x]=base_map[pc->y][pc->x];
          pc->x=nx; pc->y=ny;
          dungeon[ny][nx]='@';
        } else {
          display_message("Blocked!");
        }
        return;
      }
      case '8': case 'k': {
        int nx=pc->x; int ny=pc->y-1;
        if(inBounds(nx,ny) && pc_can_walk_on(dungeon[ny][nx])) {
          for(int i=0;i<num_characters;i++){
            if(characters[i].alive &&characters[i].x==nx && characters[i].y==ny && &characters[i]!=pc){
              characters[i].alive=0;
              if(characters[i].type==char_pc) pc_is_alive=0;
            }
          }
          dungeon[pc->y][pc->x]=base_map[pc->y][pc->x];
          pc->y=ny;
          dungeon[ny][nx]='@';
        } else {
          display_message("Blocked!");
        }
        return;
      }
      case '9': case 'u': {
        int nx=pc->x+1; int ny=pc->y-1;
        if(inBounds(nx,ny) && pc_can_walk_on(dungeon[ny][nx])) {
          for(int i=0;i<num_characters;i++){
            if(characters[i].alive &&characters[i].x==nx && characters[i].y==ny && &characters[i]!=pc){
              characters[i].alive=0;
              if(characters[i].type==char_pc) pc_is_alive=0;
            }
          }
          dungeon[pc->y][pc->x]=base_map[pc->y][pc->x];
          pc->x=nx; pc->y=ny;
          dungeon[ny][nx]='@';
        } else {
          display_message("Blocked!");
        }
        return;
      }
      case '6': case 'l': {
        int nx=pc->x+1; int ny=pc->y;
        if(inBounds(nx,ny) && pc_can_walk_on(dungeon[ny][nx])) {
          for(int i=0;i<num_characters;i++){
            if(characters[i].alive &&characters[i].x==nx && characters[i].y==ny && &characters[i]!=pc){
              characters[i].alive=0;
              if(characters[i].type==char_pc) pc_is_alive=0;
            }
          }
          dungeon[pc->y][pc->x]=base_map[pc->y][pc->x];
          pc->x=nx;
          dungeon[ny][nx]='@';
        } else {
          display_message("Blocked!");
        }
        return;
      }
      case '3': case 'n': {
        int nx=pc->x+1; int ny=pc->y+1;
        if(inBounds(nx,ny) && pc_can_walk_on(dungeon[ny][nx])) {
          for(int i=0;i<num_characters;i++){
            if(characters[i].alive &&characters[i].x==nx && characters[i].y==ny && &characters[i]!=pc){
              characters[i].alive=0;
              if(characters[i].type==char_pc) pc_is_alive=0;
            }
          }
          dungeon[pc->y][pc->x]=base_map[pc->y][pc->x];
          pc->x=nx; pc->y=ny;
          dungeon[ny][nx]='@';
        } else {
          display_message("Blocked!");
        }
        return;
      }
      case '2': case 'j': {
        int nx=pc->x; int ny=pc->y+1;
        if(inBounds(nx,ny) && pc_can_walk_on(dungeon[ny][nx])) {
          for(int i=0;i<num_characters;i++){
            if(characters[i].alive &&characters[i].x==nx && characters[i].y==ny && &characters[i]!=pc){
              characters[i].alive=0;
              if(characters[i].type==char_pc) pc_is_alive=0;
            }
          }
          dungeon[pc->y][pc->x]=base_map[pc->y][pc->x];
          pc->y=ny;
          dungeon[ny][nx]='@';
        } else {
          display_message("Blocked!");
        }
        return;
      }
      case '1': case 'b': {
        int nx=pc->x-1; int ny=pc->y+1;
        if(inBounds(nx,ny) && pc_can_walk_on(dungeon[ny][nx])) {
          for(int i=0;i<num_characters;i++){
            if(characters[i].alive &&characters[i].x==nx && characters[i].y==ny && &characters[i]!=pc){
              characters[i].alive=0;
              if(characters[i].type==char_pc) pc_is_alive=0;
            }
          }
          dungeon[pc->y][pc->x]=base_map[pc->y][pc->x];
          pc->x=nx; pc->y=ny;
          dungeon[ny][nx]='@';
        } else {
          display_message("Blocked!");
        }
        return;
      }
      case '4': case 'h': {
        int nx=pc->x-1; int ny=pc->y;
        if(inBounds(nx,ny) && pc_can_walk_on(dungeon[ny][nx])) {
          for(int i=0;i<num_characters;i++){
            if(characters[i].alive &&characters[i].x==nx && characters[i].y==ny && &characters[i]!=pc){
              characters[i].alive=0;
              if(characters[i].type==char_pc) pc_is_alive=0;
            }
          }
          dungeon[pc->y][pc->x]=base_map[pc->y][pc->x];
          pc->x=nx;
          dungeon[ny][nx]='@';
        } else {
          display_message("Blocked!");
        }
        return;
      }
      // Stairs: check the **base_map** so that overwriting with '@' doesn't break logic
      case '>': {
        if(base_map[pc->y][pc->x] == '>'){
          new_level(global_num_monsters);
          display_message("You went down the stairs...");
        } else {
          display_message("No downward staircase here!");
        }
        return;
      }
      case '<': {
        if(base_map[pc->y][pc->x] == '<'){
          new_level(global_num_monsters);
          display_message("You went up the stairs...");
        } else {
          display_message("No upward staircase here!");
        }
        return;
      }
      case '5': case ' ': case '.':
        display_message("You rest.");
        return;
      case 'm':
        display_monster_list();
        break;  // show list but do not consume turn
      case 'Q':
        pc_is_alive=0;
        return;
      default:
        break;
    }
  }
}

static void placePC(int x,int y){
  dungeon[y][x]='@';
}

// ----------------------------------------------------------------
// --------------------- File I/O, load/save -----------------------
// ----------------------------------------------------------------
void load_dungeon(const char *path)
{
    FILE *f = fopen(path, "rb");
    if(!f){
      fprintf(stderr, "Error opening file: %s\n", path);
      exit(1);
    }
    char marker[MARKER_LEN+1];
    fread(marker,1,MARKER_LEN,f);
    marker[MARKER_LEN]='\0';
    if(strcmp(marker,"RLG327-S2025")!=0){
      fprintf(stderr,"Invalid marker\n");
      fclose(f);
      exit(1);
    }
    uint32_t version; fread(&version,sizeof(version),1,f);
    version=be32toh(version);
    if(version!=FILE_VERSION){
      fprintf(stderr,"Unsupported file version\n");
      fclose(f);
      exit(1);
    }
    uint32_t file_size; fread(&file_size,sizeof(file_size),1,f);
    file_size=be32toh(file_size);

    uint8_t pcx,pcy;
    fread(&pcx,1,1,f);
    fread(&pcy,1,1,f);
    pc_x=pcx; pc_y=pcy;

    for(int y=0;y<HEIGHT;y++){
      for(int x=0;x<WIDTH;x++){
        uint8_t h; fread(&h,1,1,f);
        hardness[y][x]=h;
      }
    }
    uint16_t r; fread(&r,sizeof(r),1,f);
    r=be16toh(r);
    room_count=0;
    for(int i=0;i<r&&i<MAX_ROOMS;i++){
      uint8_t rx,ry,rw,rh;
      fread(&rx,1,1,f); fread(&ry,1,1,f);
      fread(&rw,1,1,f); fread(&rh,1,1,f);
      room_x[i]=rx; room_y[i]=ry;
      room_w[i]=rw; room_h[i]=rh;
      room_count++;
    }
    uint16_t u; fread(&u,sizeof(u),1,f);
    u=be16toh(u); upCount=0;
    if(u>0){
      uint8_t sx,sy; fread(&sx,1,1,f); fread(&sy,1,1,f);
      upCount=1; up_xCoord=sx; up_yCoord=sy;
    }
    uint16_t d; fread(&d,sizeof(d),1,f);
    d=be16toh(d); downCount=0;
    if(d>0){
      uint8_t sx,sy; fread(&sx,1,1,f); fread(&sy,1,1,f);
      downCount=1; down_xCoord=sx; down_yCoord=sy;
    }
    fclose(f);

    for(int yy=0;yy<HEIGHT;yy++){
      for(int xx=0;xx<WIDTH;xx++){
        if(hardness[yy][xx]==255) dungeon[yy][xx]=' ';
        else if(hardness[yy][xx]>0) dungeon[yy][xx]=' ';
        else dungeon[yy][xx]='#';
      }
    }
    for(int i=0;i<room_count;i++){
      for(int row=room_y[i];row<room_y[i]+room_h[i];row++){
        for(int col=room_x[i];col<room_x[i]+room_w[i];col++){
          dungeon[row][col]='.';
        }
      }
    }
    if(upCount>0)   dungeon[ up_yCoord ][ up_xCoord ]='<';
    if(downCount>0) dungeon[ down_yCoord ][ down_xCoord ]='>';
}

void save_dungeon(const char*path)
{
    FILE *f=fopen(path,"wb");
    if(!f){
      fprintf(stderr,"Err open file\n");
      return;
    }
    fwrite(FILE_MARKER,1,MARKER_LEN,f);
    uint32_t version_be=htobe32(FILE_VERSION);
    fwrite(&version_be,sizeof(version_be),1,f);

    uint16_t up_stairs_count=(upCount>0)?1:0;
    uint16_t down_stairs_count=(downCount>0)?1:0;

    uint32_t file_size=1702+(room_count*4)+2+(up_stairs_count*2)+2+(down_stairs_count*2);
    uint32_t file_size_be=htobe32(file_size);
    fwrite(&file_size_be,sizeof(file_size_be),1,f);

    uint8_t pcx=pc_x, pcy=pc_y;
    fwrite(&pcx,1,1,f);
    fwrite(&pcy,1,1,f);

    for(int y=0;y<HEIGHT;y++){
      for(int x=0;x<WIDTH;x++){
        uint8_t hh=hardness[y][x];
        fwrite(&hh,1,1,f);
      }
    }
    uint16_t r_be=htobe16(room_count);
    fwrite(&r_be,sizeof(r_be),1,f);
    for(int i=0;i<room_count;i++){
      uint8_t rx=room_x[i], ry=room_y[i], rw=room_w[i], rh=room_h[i];
      fwrite(&rx,1,1,f);
      fwrite(&ry,1,1,f);
      fwrite(&rw,1,1,f);
      fwrite(&rh,1,1,f);
    }
    uint16_t up_be=htobe16(up_stairs_count);
    fwrite(&up_be,sizeof(up_be),1,f);
    if(up_stairs_count==1){
      uint8_t sx=(uint8_t)up_xCoord, sy=(uint8_t)up_yCoord;
      fwrite(&sx,1,1,f); fwrite(&sy,1,1,f);
    }
    uint16_t down_be=htobe16(downCount>0?1:0);
    fwrite(&down_be,sizeof(down_be),1,f);
    if(downCount>0){
      uint8_t sx=(uint8_t)down_xCoord, sy=(uint8_t)down_yCoord;
      fwrite(&sx,1,1,f); fwrite(&sy,1,1,f);
    }
    fclose(f);
}

// ----------------------------------------------------------------
// ------------------------- MAIN ----------------------------------
// ----------------------------------------------------------------
int main(int argc,char*argv[])
{
  srand(time(NULL));
  int load=0, save=0;
  int local_num_mon = DEFAULT_NUMMON;

  for(int i=1;i<argc;i++){
    if(!strcmp(argv[i],"--load")) load=1;
    else if(!strcmp(argv[i],"--save")) save=1;
    else if(!strcmp(argv[i],"--nummon") && i+1<argc){
      local_num_mon=atoi(argv[++i]);
    }
  }
  global_num_monsters=local_num_mon;

  checkDir();
  char path[1024];
  getPath(path,sizeof(path));

  if(load){
    load_dungeon(path);
  } else {
    initializeDungeon();
    generateRooms();
    connectRoomsViaCorridor();
    placeStairs();
    if(room_count>0){
      pc_x=room_x[0];
      pc_y=room_y[0];
    } else {
      pc_x=1; pc_y=1;
    }
  }

  memcpy(base_map,dungeon,sizeof(base_map));
  placePC(pc_x,pc_y);

  if(save){
    save_dungeon(path);
  }

  djikstraForNonTunnel(pc_x,pc_y);
  djikstraForTunnel(pc_x,pc_y);

  num_characters=0;
  create_pc();
  for(int i=0;i<local_num_mon;i++){
    create_monster();
  }

  event_queue_t eq;
  init_event_queue(&eq,(local_num_mon+1)*10);

  int aliveMonsters=local_num_mon;
  int current_time=0;
  for(int i=0;i<num_characters;i++){
    event_t e; e.time=0; e.c=&characters[i];
    eq_push(&eq,e);
  }

  init_curses();
  display_message("Welcome to the roguelike. Use movement keys, '>' '<', 'm', 'Q', etc.");

  while(!eq_empty(&eq)&&pc_is_alive&&aliveMonsters>0){
    event_t e=eq_pop(&eq);
    current_time=e.time;
    character_t*c=e.c;

    if(!c->alive) continue;

    if(c->type==char_pc){
      display_dungeon();
      handle_pc_input(c);

      if(pc_is_alive){
        pc_x=c->x; pc_y=c->y;
        djikstraForNonTunnel(pc_x,pc_y);
        djikstraForTunnel(pc_x,pc_y);
      }

    } else {
      do_monster_movement(c);
      if(!pc_is_alive) break;
      if(!c->alive) aliveMonsters--;
    }

    if(c->alive){
      int next_time=current_time+(1000/c->speed);
      event_t ne; ne.time=next_time; ne.c=c;
      eq_push(&eq,ne);
    }
  }

  if(!pc_is_alive){
    display_dungeon();
    display_message("You lose! The PC has been killed.");
  } else if(aliveMonsters==0){
    display_dungeon();
    display_message("You win! All monsters have been slain.");
  } else {
    display_message("Simulation ended early (queue empty?).");
  }

  getch();
  end_curses();

  del_event_queue(&eq);
  return 0;
}

// ----------------------------------------------------------------
// -------------------- checkDir & getPath -------------------------
// ----------------------------------------------------------------
void checkDir(){
  char*home=getenv("HOME");
  if(!home){
    fprintf(stderr,"ERROR: No HOME env var.\n");
    exit(1);
  }
  char path[1024];
  snprintf(path,sizeof(path),"%s%s",home,DUNGEON_DIR);
  if(mkdir(path,0700)&&errno!=EEXIST){
    fprintf(stderr,"ERROR creating %s: %s\n",path,strerror(errno));
    exit(1);
  }
}

void getPath(char*buf,size_t size){
  char*home=getenv("HOME");
  snprintf(buf,size,"%s%s%s",home,DUNGEON_DIR,DUNGEON_FILE);
}
