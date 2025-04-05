#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <string>
#include <vector>
#include <limits>
#include <algorithm>

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
static const char *DUNGEON_DIR   = "/.rlg327/";
static const char *DUNGEON_FILE  = "dungeon";
static const char *FILE_MARKER   = "RLG327-S2025";
static const int   MARKER_LEN    = 12;
static const int   FILE_VERSION  = 0;
static const int   WIDTH         = 80;
static const int   HEIGHT        = 21;
static const int   MAX_ROOMS     = 10;
static const int   DEFAULT_NUMMON = 10;

// "Fog of War" radius
static const int   PC_LIGHT_RADIUS = 3;

// Forward declarations
class Dungeon;
class Character {
public:
    enum CharType {
        PC_TYPE,
        NPC_TYPE
    };

    CharType   type;
    bool       alive;
    int        x, y;       // Position
    int        speed;
    int        turn;       // Next turn time
    int        hp;
    uint8_t    btype;      // Monster behavior bit flags (for NPCs only)
    char       symbol;

    Character()
        : type(NPC_TYPE), alive(true), x(0), y(0), speed(10), turn(0),
          hp(10), btype(0), symbol('?')
    {}

    virtual ~Character() {}

    // Each derived class must implement doTurn()
    virtual void doTurn(Dungeon &d) = 0;
};

class PC : public Character {
public:
    // The PC will maintain its own memory of the terrain
    char remembered_map[HEIGHT][WIDTH];
    // Whether to show no fog
    bool noFog;
    // Are we currently in teleport mode?
    bool teleporting;
    // Teleport-cursor position
    int teleportXCoordinates, teleportYCoordinates;

    PC() {
        type = PC_TYPE;
        alive = true;
        speed = 10;
        hp = 50; // default HP
        symbol = '@';
        btype = 0; // PC has no monster bitflags
        noFog = false;
        teleporting = false;
        // Initialize remembered_map to spaces
        for(int r = 0; r < HEIGHT; r++){
            for(int c = 0; c < WIDTH; c++){
                remembered_map[r][c] = ' ';
            }
        }
    }

    virtual void doTurn(Dungeon &d) override;

    // Update the PC's remembered map based on visibility
    void updateRemembered(Dungeon &d);

    // Check if cell (x2,y2) is visible to PC (within radius)
    bool isVisible(int x2, int y2) const {
        int dx = x2 - x;
        int dy = y2 - y;
        // Euclidean distance <= PC_LIGHT_RADIUS
        return (dx*dx + dy*dy) <= (PC_LIGHT_RADIUS*PC_LIGHT_RADIUS);
    }
};

class NPC : public Character {
public:
    NPC(uint8_t behavior_flags, int start_x, int start_y, int spd, int health)
    {
        type   = NPC_TYPE;
        alive  = true;
        x      = start_x;
        y      = start_y;
        btype  = behavior_flags;
        speed  = spd;
        turn   = 0;
        hp     = health;
        // symbol is determined from btype’s lower nibble, e.g. [0..15 -> hex]
        static const char *hex_map = "0123456789abcdef";
        symbol = hex_map[btype & 0x0F];
    }

    virtual void doTurn(Dungeon &d) override;
};

struct Room {
    int x, y;
    int w, h;
};

struct Event {
    int        time;
    Character *c;
};

class EventQueue {
public:
    std::vector<Event> heap;

    EventQueue() {
        heap.reserve(1024); // arbitrary
    }

    bool empty() const { return heap.empty(); }

    void push(const Event &e) {
        heap.push_back(e);
        heapifyUp(heap.size() - 1);
    }

    Event pop() {
        Event top = heap[0];
        heap[0] = heap.back();
        heap.pop_back();
        heapifyDown(0);
        return top;
    }

private:
    static int parentIdx(int i) { return (i - 1) / 2; }
    static int leftIdx(int i)   { return 2*i + 1; }
    static int rightIdx(int i)  { return 2*i + 2; }

    void heapifyUp(int i) {
        while(i > 0){
            int p = parentIdx(i);
            if(heap[i].time < heap[p].time){
                std::swap(heap[i], heap[p]);
                i = p;
            } else {
                break;
            }
        }
    }
    void heapifyDown(int i) {
        while(true){
            int l = leftIdx(i);
            int r = rightIdx(i);
            int min_i = i;
            if(l < (int)heap.size() && heap[l].time < heap[min_i].time) {
                min_i = l;
            }
            if(r < (int)heap.size() && heap[r].time < heap[min_i].time) {
                min_i = r;
            }
            if(min_i == i) break;
            std::swap(heap[i], heap[min_i]);
            i = min_i;
        }
    }
};

struct Node {
    int x, y;
    int dist;
};

class NodeHeap {
public:
    std::vector<Node> array;

    NodeHeap() {
        array.reserve(WIDTH * HEIGHT);
    }
    void insert(const Node &n) {
        array.push_back(n);
        heapifyUp(array.size() - 1);
    }
    bool empty() const { return array.empty(); }

    Node pop() {
        Node top = array[0];
        array[0] = array.back();
        array.pop_back();
        heapifyDown(0);
        return top;
    }

private:
    static int parentIdx(int i) { return (i - 1) / 2; }
    static int leftIdx(int i)   { return 2*i + 1; }
    static int rightIdx(int i)  { return 2*i + 2; }

    void heapifyUp(int i) {
        while(i > 0){
            int p = parentIdx(i);
            if(array[i].dist < array[p].dist){
                std::swap(array[i], array[p]);
                i = p;
            } else {
                break;
            }
        }
    }

    void heapifyDown(int i) {
        while(true){
            int l = leftIdx(i);
            int r = rightIdx(i);
            int min_i = i;
            if(l < (int)array.size() && array[l].dist < array[min_i].dist) {
                min_i = l;
            }
            if(r < (int)array.size() && array[r].dist < array[min_i].dist) {
                min_i = r;
            }
            if(min_i == i) break;
            std::swap(array[i], array[min_i]);
            i = min_i;
        }
    }
};

class Dungeon {
public:
    int hardness[HEIGHT][WIDTH];
    char base_map[HEIGHT][WIDTH];
    char dungeon[HEIGHT][WIDTH];
    int disTunneling[HEIGHT][WIDTH];
    int disNonTunneling[HEIGHT][WIDTH];

    
    int pc_x, pc_y;

    // Rooms
    Room rooms[MAX_ROOMS];
    int  room_count;

    // Stair data
    int upCount, downCount;
    int up_xCoord, up_yCoord;
    int down_xCoord, down_yCoord;
    int global_num_monsters;
    std::vector<Character*> characters;

    // PC alive or not
    bool pc_is_alive;
    bool changedFloor;

    Dungeon() {
        pc_is_alive = true;
        global_num_monsters = DEFAULT_NUMMON;
        upCount = downCount = 0;
        up_xCoord = up_yCoord = 0;
        down_xCoord = down_yCoord = 0;
        room_count = 0;
        pc_x = pc_y = 0;
        changedFloor = false;

        // Clear arrays
        for(int r=0; r<HEIGHT; r++){
            for(int c=0; c<WIDTH; c++){
                hardness[r][c] = 0;
                base_map[r][c] = ' ';
                dungeon[r][c]  = ' ';
                disTunneling[r][c] = INT32_MAX;
                disNonTunneling[r][c] = INT32_MAX;
            }
        }
    }

    ~Dungeon() {
        for(auto c : characters) {
            delete c;
        }
        characters.clear();
    }

    bool inBounds(int x, int y) const {
        return (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT);
    }
    bool isImmutableRock(int x, int y) const {
        return hardness[y][x] == 255;
    }
    bool isFloor(int x, int y) const {
        char c = base_map[y][x];
        return (c == '.' || c == '#' || c == '<' || c == '>');
    }
    bool pcCanWalkOn(char cell) const {
        return (cell == '.' || cell == '#' || cell == '<' || cell == '>');
    }

    void rebuildDisplay() {
        memcpy(dungeon, base_map, sizeof(dungeon));
        for (auto c : characters) {
            if(c->alive) {
                dungeon[c->y][c->x] = c->symbol;
            }
        }
    }

    // Dijkstra for tunnelers
    void djikstraForTunnel(int x, int y) {
        for(int i=0; i<HEIGHT; i++){
            for(int j=0; j<WIDTH; j++){
                disTunneling[i][j] = INT32_MAX;
            }
        }
        disTunneling[y][x] = 0;
        NodeHeap h;
        h.insert(Node{x, y, 0});

        int dirs[8][2] = {
            {-1,0},{1,0},{0,-1},{0,1},
            {-1,-1},{-1,1},{1,-1},{1,1}
        };

        while(!h.empty()) {
            Node u = h.pop();
            if(u.dist > disTunneling[u.y][u.x]) continue;

            for(int i=0; i<8; i++){
                int nx = u.x + dirs[i][0];
                int ny = u.y + dirs[i][1];
                if(!inBounds(nx, ny)) continue;
                if(hardness[ny][nx] != 255) {
                    int cost = 1;
                    if(hardness[ny][nx] > 0 && hardness[ny][nx] < 255){
                        cost += hardness[ny][nx]/85;
                    }
                    int alt = u.dist + cost;
                    if(alt < disTunneling[ny][nx]) {
                        disTunneling[ny][nx] = alt;
                        h.insert(Node{nx, ny, alt});
                    }
                }
            }
        }
    }

    // Dijkstra for non-tunnelers
    void djikstraForNonTunnel(int x, int y) {
        for(int r=0; r<HEIGHT; r++){
            for(int c=0; c<WIDTH; c++){
                disNonTunneling[r][c] = INT32_MAX;
            }
        }
        disNonTunneling[y][x] = 0;

        NodeHeap h;
        h.insert(Node{x, y, 0});

        int dirs[8][2] = {
            {-1,0},{1,0},{0,-1},{0,1},
            {-1,-1},{-1,1},{1,-1},{1,1}
        };

        while(!h.empty()) {
            Node u = h.pop();
            if(u.dist > disNonTunneling[u.y][u.x]) continue;

            for(int i=0; i<8; i++){
                int nx = u.x + dirs[i][0];
                int ny = u.y + dirs[i][1];
                if(!inBounds(nx, ny)) continue;
                // non-tunnelers only pass if hardness[ny][nx] == 0
                if(hardness[ny][nx] == 0) {
                    int alt = u.dist + 1;
                    if(alt < disNonTunneling[ny][nx]) {
                        disNonTunneling[ny][nx] = alt;
                        h.insert(Node{nx, ny, alt});
                    }
                }
            }
        }
    }

    PC* getPC() {
        for(auto c : characters) {
            if(c->type == Character::PC_TYPE) {
                return dynamic_cast<PC*>(c);
            }
        }
        return nullptr;
    }

    int countMonsters() const {
        int count = 0;
        for(auto c : characters) {
            if(c->type == Character::NPC_TYPE && c->alive) {
                count++;
            }
        }
        return count;
    }

    // Create PC
    void createPC(int px, int py) {
        PC *pc = new PC();
        pc->x = px;
        pc->y = py;
        characters.push_back(pc);
    }

    // Create a monster on a random '.' location
    void createMonster() {
        int rx, ry;
        do {
            rx = rand() % WIDTH;
            ry = rand() % HEIGHT;
        } while(base_map[ry][rx] != '.');
        uint8_t flags = rand() & 0x0F;
        int spd = (rand()%16) + 5;
        int mhp = 10;
        NPC *m = new NPC(flags, rx, ry, spd, mhp);
        characters.push_back(m);
    }

    // The main event loop
    void gameLoop() {
        EventQueue eq;

        // Insert all alive characters with next turn = 0
        for(auto c : characters) {
            if(c->alive) {
                Event e;
                e.time = 0;
                e.c    = c;
                eq.push(e);
            }
        }
        int aliveMonsters = countMonsters();
        int current_time  = 0;
        changedFloor      = false; // reset each time we do a fresh loop
        while(!eq.empty() && pc_is_alive && aliveMonsters > 0 && !changedFloor) {
            Event e = eq.pop();
            current_time = e.time;
            Character *chr = e.c;
            if(!chr->alive) {
                continue;
            }
            chr->doTurn(*this);

            if(!pc_is_alive) {
                break;
            }
            if(chr->alive && !changedFloor) {
                int next_time = current_time + (1000 / chr->speed);
                Event ne { next_time, chr };
                eq.push(ne);
            }

            aliveMonsters = countMonsters();
        }
    }
    void newLevel(int nummon) {
        // Delete all existing characters and clear the vector to avoid double free.
        for(auto c : characters) {
            delete c;
        }
        characters.clear();

        // Reset the dungeon map and hardness.
        for(int y=0; y<HEIGHT; y++){
            for(int x=0; x<WIDTH; x++){
                if(x==0 || x==WIDTH-1 || y==0 || y==HEIGHT-1){
                    hardness[y][x] = 255;
                    base_map[y][x] = ' ';
                } else {
                    hardness[y][x] = (rand() % 254) + 1;
                    base_map[y][x] = ' ';
                }
            }
        }
        room_count = 0;
        upCount = 0;
        downCount = 0;
        extern void generateRooms(Dungeon &d);
        extern void connectRoomsViaCorridor(Dungeon &d);
        extern void placeStairs(Dungeon &d);
        generateRooms(*this);
        connectRoomsViaCorridor(*this);
        placeStairs(*this);

        if(room_count > 0) {
            pc_x = rooms[0].x;
            pc_y = rooms[0].y;
        } else {
            pc_x = 1;
            pc_y = 1;
        }
        memcpy(dungeon, base_map, sizeof(dungeon));

        createPC(pc_x, pc_y);

        for(int i=0; i<nummon; i++){
            createMonster();
        }
        pc_is_alive = true;
        djikstraForNonTunnel(pc_x, pc_y);
        djikstraForTunnel(pc_x, pc_y);
    }
};

void PC::doTurn(Dungeon &d) {
    d.djikstraForNonTunnel(x, y);
    d.djikstraForTunnel(x, y);

    d.rebuildDisplay();
    updateRemembered(d);

    clear();
    bool showAll = (noFog || teleporting);
    for(int r=0; r<HEIGHT; r++){
        move(r, 0);
        for(int c=0; c<WIDTH; c++){
            if(r == y && c == x) {
                addch('@');
            } else {
                if(showAll) {
                    addch(d.dungeon[r][c]);
                } else {
                    if(isVisible(c, r)) {
                        addch(d.dungeon[r][c]);
                    } else {
                        addch(remembered_map[r][c]);
                    }
                }
            }
        }
    }
    move(0,0);
    if(!teleporting) {
        printw("PC turn. (hjklyubn etc) 'f'=fog, 'g'=teleport, 'm'=list, 'Q'=quit");
    } else {
        printw("TELEPORT mode. Move cursor; 'g' or 'r' teleports, 'Q' quits");
    }
    refresh();
    while(true) {
        int ch = getch();
        if(teleporting) {
            // TELEPORT MODE
            switch(ch) {
                case '7': case 'y':
                    if(d.inBounds(teleportXCoordinates-1, teleportYCoordinates-1)) {
                        teleportXCoordinates--;
                        teleportYCoordinates--;
                    }
                    break;
                case '9': case 'u':
                    if(d.inBounds(teleportXCoordinates+1, teleportYCoordinates-1)) {
                        teleportXCoordinates++;
                        teleportYCoordinates--;
                    }
                    break;
                case '1': case 'b':
                    if(d.inBounds(teleportXCoordinates-1, teleportYCoordinates+1)) {
                        teleportXCoordinates--;
                        teleportYCoordinates++;
                    }
                    break;
                case '3': case 'n':
                    if(d.inBounds(teleportXCoordinates+1, teleportYCoordinates+1)) {
                        teleportXCoordinates++;
                        teleportYCoordinates++;
                    }
                    break;
                case '4': case 'h':
                    if(d.inBounds(teleportXCoordinates-1, teleportYCoordinates)) {
                        teleportXCoordinates--;
                    }
                    break;
                case '6': case 'l':
                    if(d.inBounds(teleportXCoordinates+1, teleportYCoordinates)) {
                        teleportXCoordinates++;
                    }
                    break;
                case '8': case 'k':
                    if(d.inBounds(teleportXCoordinates, teleportYCoordinates-1)) {
                        teleportYCoordinates--;
                    }
                    break;
                case '2': case 'j':
                    if(d.inBounds(teleportXCoordinates, teleportYCoordinates+1)) {
                        teleportYCoordinates++;
                    }
                    break;
                case '5': case ' ':
                case '.':
                    // do nothing
                    break;
                case 'g':
                    if(!d.isImmutableRock(teleportXCoordinates, teleportYCoordinates)) {
                        x = teleportXCoordinates;
                        y = teleportYCoordinates;
                    }
                    teleporting = false;
                    return; // used turn
                case 'r':
                    while(true) {
                        int rx = rand()%WIDTH;
                        int ry = rand()%HEIGHT;
                        if(!d.isImmutableRock(rx, ry)) {
                            x = rx;
                            y = ry;
                            break;
                        }
                    }
                    teleporting = false;
                    return; 
                case 'f':
                    noFog = !noFog;
                    break;
                case 'Q':
                    alive = false;
                    d.pc_is_alive = false;
                    return;
                default:
                    break;
            }
            // Re-draw with '*'
            d.rebuildDisplay();
            clear();
            for(int r=0; r<HEIGHT; r++){
                move(r,0);
                for(int c=0; c<WIDTH; c++){
                    if(r == y && c == x) {
                        addch('@');
                    } else if(r == teleportYCoordinates && c == teleportXCoordinates) {
                        addch('*');
                    } else {
                        if(noFog) {
                            addch(d.dungeon[r][c]);
                        } else {
                            if(isVisible(c, r)) {
                                addch(d.dungeon[r][c]);
                            } else {
                                addch(remembered_map[r][c]);
                            }
                        }
                    }
                }
            }
            move(0,0);
            printw("TELEPORT mode. Move '*'. 'g'=teleport, 'r'=random, 'f'=fog, 'Q'=quit.");
            refresh();
        } else {
            // NORMAL MODE
            switch(ch) {
                // Diagonal + cardinal moves
                case '7': case 'y': {
                    int nx = x-1, ny = y-1;
                    if(d.inBounds(nx, ny) && d.pcCanWalkOn(d.base_map[ny][nx])) {
                        // Attack monster if present
                        for(auto &c : d.characters){
                            if(c->alive && c->x == nx && c->y == ny && c != this) {
                                c->alive = false;
                                if(c->type == PC_TYPE) {
                                    d.pc_is_alive = false;
                                }
                            }
                        }
                        x = nx; y = ny;
                    }
                    return; 
                }
                case '8': case 'k': {
                    int nx = x, ny = y-1;
                    if(d.inBounds(nx, ny) && d.pcCanWalkOn(d.base_map[ny][nx])) {
                        for(auto &c : d.characters){
                            if(c->alive && c->x == nx && c->y == ny && c != this) {
                                c->alive = false;
                                if(c->type == PC_TYPE) d.pc_is_alive = false;
                            }
                        }
                        x = nx; y = ny;
                    }
                    return;
                }
                case '9': case 'u': {
                    int nx = x+1, ny = y-1;
                    if(d.inBounds(nx, ny) && d.pcCanWalkOn(d.base_map[ny][nx])) {
                        for(auto &c : d.characters){
                            if(c->alive && c->x == nx && c->y == ny && c != this) {
                                c->alive = false;
                                if(c->type == PC_TYPE) d.pc_is_alive = false;
                            }
                        }
                        x = nx; y = ny;
                    }
                    return;
                }
                case '6': case 'l': {
                    int nx = x+1, ny = y;
                    if(d.inBounds(nx, ny) && d.pcCanWalkOn(d.base_map[ny][nx])) {
                        for(auto &c : d.characters){
                            if(c->alive && c->x == nx && c->y == ny && c != this) {
                                c->alive = false;
                                if(c->type == PC_TYPE) d.pc_is_alive = false;
                            }
                        }
                        x = nx; y = ny;
                    }
                    return;
                }
                case '3': case 'n': {
                    int nx = x+1, ny = y+1;
                    if(d.inBounds(nx, ny) && d.pcCanWalkOn(d.base_map[ny][nx])) {
                        for(auto &cc : d.characters){
                            if(cc->alive && cc->x == nx && cc->y == ny && cc != this) {
                                cc->alive = false;
                            }
                        }
                        x = nx; y = ny;
                    }
                    return;
                }
                case '2': case 'j': {
                    int nx = x, ny = y+1;
                    if(d.inBounds(nx, ny) && d.pcCanWalkOn(d.base_map[ny][nx])) {
                        for(auto &cc : d.characters){
                            if(cc->alive && cc->x == nx && cc->y == ny && cc != this) {
                                cc->alive = false;
                            }
                        }
                        x = nx; y = ny;
                    }
                    return;
                }
                case '1': case 'b': {
                    int nx = x-1, ny = y+1;
                    if(d.inBounds(nx, ny) && d.pcCanWalkOn(d.base_map[ny][nx])) {
                        for(auto &cc : d.characters){
                            if(cc->alive && cc->x == nx && cc->y == ny && cc != this) {
                                cc->alive = false;
                            }
                        }
                        x = nx; y = ny;
                    }
                    return;
                }
                case '4': case 'h': {
                    int nx = x-1, ny = y;
                    if(d.inBounds(nx, ny) && d.pcCanWalkOn(d.base_map[ny][nx])) {
                        for(auto &cc : d.characters){
                            if(cc->alive && cc->x == nx && cc->y == ny && cc != this) {
                                cc->alive = false;
                            }
                        }
                        x = nx; y = ny;
                    }
                    return;
                }
                case '5': case ' ': case '.':
                    // rest
                    return;

                // Stairs
                case '>': {
                    if(d.base_map[y][x] == '>') {
                        d.changedFloor = true;
                    }
                    return; 
                }
                case '<': {
                    if(d.base_map[y][x] == '<') {
                        d.changedFloor = true;
                    }
                    return;
                }
                case 'm': {
                    clear();
                    printw("--- Monster List (ESC=exit, up/down=scroll) ---\n");
                    std::vector<std::string> lines;
                    for(auto c : d.characters) {
                        if(c->type == Character::NPC_TYPE && c->alive) {
                            int dx = c->x - x;
                            int dy = c->y - y;
                            std::string dir;
                            if(dy < 0) dir += std::to_string(-dy) + " north ";
                            if(dy > 0) dir += std::to_string(dy) + " south ";
                            if(dx < 0) dir += std::to_string(-dx) + " west ";
                            if(dx > 0) dir += std::to_string(dx) + " east ";
                            if(dx == 0 && dy == 0) dir = "Same cell??";
                            char buf[128];
                            snprintf(buf, sizeof(buf), "%c: %s", c->symbol, dir.c_str());
                            lines.push_back(buf);
                        }
                    }
                    int offset = 0;
                    const int LINES_AVAIL = 20;
                    bool done = false;
                    while(!done) {
                        clear();
                        mvprintw(0,0,"--- Monster List (ESC=exit, up/down=scroll) ---");
                        int line=1;
                        for(int i=0; i<LINES_AVAIL; i++){
                            int idx = offset + i;
                            if(idx >= (int)lines.size()) break;
                            mvprintw(line++, 0, "%s", lines[idx].c_str());
                        }
                        refresh();
                        int ckey = getch();
                        switch(ckey) {
                            case 27:
                                done=true; break;
                            case KEY_UP:
                                if(offset > 0) offset--;
                                break;
                            case KEY_DOWN:
                                if(offset + LINES_AVAIL < (int)lines.size()) offset++;
                                break;
                            default:
                                break;
                        }
                    }
                    // re-draw
                    d.rebuildDisplay();
                    return;
                }
                case 'f':
                    noFog = !noFog;
                    return;
                case 'g':
                    teleporting = true;
                    teleportXCoordinates = x;
                    teleportYCoordinates = y;
                    return;
                case 'Q':
                    alive = false;
                    d.pc_is_alive = false;
                    return;
                default:
                    // ignore
                    break;
            }
        }
    }
}
void PC::updateRemembered(Dungeon &d) {
    for(int ry = y - PC_LIGHT_RADIUS; ry <= y + PC_LIGHT_RADIUS; ry++) {
        for(int rx = x - PC_LIGHT_RADIUS; rx <= x + PC_LIGHT_RADIUS; rx++) {
            if(d.inBounds(rx, ry)) {
                if(isVisible(rx, ry)) {
                    remembered_map[ry][rx] = d.base_map[ry][rx];
                }
            }
        }
    }
}
void NPC::doTurn(Dungeon &d) {
    if(!alive) return;

    bool intelligence = (btype & 0x1);
    bool telepathic   = (btype & 0x2);
    bool tunneling    = (btype & 0x4);
    bool erratic      = (btype & 0x8);

    bool do_random = false;
    if(erratic && (rand()%2 == 0)) {
        do_random = true;
    }
    int bestx = x;
    int besty = y;

    if(do_random) {
        int rr = rand() % 9;
        static int ddx[9] = {0,-1,1,0,0,-1,-1,1,1};
        static int ddy[9] = {0,0,0,-1,1,-1,1,-1,1};
        bestx = x + ddx[rr];
        besty = y + ddy[rr];
    } else if(!intelligence) {
        int dx = (d.pc_x > x)? 1 : ((d.pc_x < x)? -1 : 0);
        int dy = (d.pc_y > y)? 1 : ((d.pc_y < y)? -1 : 0);
        bestx = x + dx;
        besty = y + dy;
    } else {
        int bestDist = INT32_MAX;
        for(int i=-1; i<=1; i++){
            for(int j=-1; j<=1; j++){
                if(!(i==0 && j==0)) {
                    int nx = x + j;
                    int ny = y + i;
                    if(d.inBounds(nx, ny)) {
                        int dval = tunneling ? d.disTunneling[ny][nx]
                                             : d.disNonTunneling[ny][nx];
                        if(dval < bestDist) {
                            bestDist = dval;
                            bestx = nx;
                            besty = ny;
                        }
                    }
                }
            }
        }
    }
    if(tunneling && d.hardness[besty][bestx] > 0 && d.hardness[besty][bestx] < 255) {
        d.hardness[besty][bestx] -= 85;
        if(d.hardness[besty][bestx] < 0) d.hardness[besty][bestx] = 0;
        if(d.hardness[besty][bestx] == 0) {
            d.base_map[besty][bestx] = '#';
        }
        return; 
    }
    if(d.pc_x == bestx && d.pc_y == besty) {
        d.pc_is_alive = false;
    }

    x = bestx;
    y = besty;
}
static void fillRoom(Dungeon &d, int w, int h, int x, int y) {
    for(int row=y; row<y+h; row++){
        for(int col=x; col<x+w; col++){
            d.base_map[row][col] = '.';
            d.hardness[row][col] = 0;
        }
    }
}

static bool isValidRoom(Dungeon &d, int w, int h, int x, int y) {
    if(w<1 || h<1 || (w+x >= WIDTH-1) || (h+y >= HEIGHT-1)) {
        return false;
    }
    for(int row=y; row<y+h; row++){
        for(int col=x; col<x+w; col++){
            if(d.base_map[row][col] != ' ') {
                return false;
            }
        }
    }
    return true;
}

void generateRooms(Dungeon &d) {
    int attempts = 2000;
    int c = 0;
    while(attempts > 0 && c < 6) {
        int rw = (rand()%6)+4;
        int rh = (rand()%4)+3;
        int rx = (rand()%(WIDTH - rw - 2))+1;
        int ry = (rand()%(HEIGHT - rh - 2))+1;
        if(isValidRoom(d, rw, rh, rx, ry)) {
            fillRoom(d, rw, rh, rx, ry);
            d.rooms[c].x = rx;
            d.rooms[c].y = ry;
            d.rooms[c].w = rw;
            d.rooms[c].h = rh;
            c++;
        }
        attempts--;
    }
    d.room_count = c;
}

void connectRoomsViaCorridor(Dungeon &d) {
    if(d.room_count < 2) return;
    for(int i=1; i<d.room_count; i++){
        int x1 = d.rooms[i-1].x + d.rooms[i-1].w/2;
        int y1 = d.rooms[i-1].y + d.rooms[i-1].h/2;
        int x2 = d.rooms[i].x + d.rooms[i].w/2;
        int y2 = d.rooms[i].y + d.rooms[i].h/2;
        while(x1 != x2){
            if(d.inBounds(x1, y1)){
                if(d.base_map[y1][x1] != '.') {
                    d.base_map[y1][x1] = '#';
                    d.hardness[y1][x1] = 0;
                }
            }
            x1 += (x2 > x1)? 1 : -1;
        }
        while(y1 != y2){
            if(d.inBounds(x1, y1)){
                if(d.base_map[y1][x1] != '.') {
                    d.base_map[y1][x1] = '#';
                    d.hardness[y1][x1] = 0;
                }
            }
            y1 += (y2 > y1)? 1 : -1;
        }
    }
}

void placeStairs(Dungeon &d) {
    bool upFlag = false;
    bool downFlag = false;
    while(!upFlag || !downFlag) {
        int up_x = rand()%WIDTH;
        int up_y = rand()%HEIGHT;
        int down_x = rand()%WIDTH;
        int down_y = rand()%HEIGHT;
        if(!upFlag) {
            if((d.base_map[up_y][up_x] == '.' || d.base_map[up_y][up_x] == '#')) {
                d.base_map[up_y][up_x] = '<';
                d.up_xCoord = up_x;
                d.up_yCoord = up_y;
                d.upCount = 1;
                upFlag = true;
            }
        }
        if(!downFlag) {
            if((d.base_map[down_y][down_x] == '.' || d.base_map[down_y][down_x] == '#')
               && !(up_x == down_x && up_y == down_y)){
                d.base_map[down_y][down_x] = '>';
                d.down_xCoord = down_x;
                d.down_yCoord = down_y;
                d.downCount = 1;
                downFlag = true;
            }
        }
    }
}

static void checkDir() {
    char* home = getenv("HOME");
    if(!home){
        std::cerr << "ERROR: No HOME env var." << std::endl;
        exit(1);
    }
    char path[1024];
    snprintf(path,sizeof(path), "%s%s", home, DUNGEON_DIR);
    if(mkdir(path,0700) && errno != EEXIST) {
        std::cerr << "ERROR creating " << path << ": " << strerror(errno) << std::endl;
        exit(1);
    }
}

static void getPath(char* buf, size_t size) {
    char* home = getenv("HOME");
    snprintf(buf,size, "%s%s%s", home, DUNGEON_DIR, DUNGEON_FILE);
}

static void save_dungeon(Dungeon &d, const char* path) {
    FILE *f = fopen(path, "wb");
    if(!f) {
        std::cerr << "Error opening " << path << " for write\n";
        return;
    }
    fwrite(FILE_MARKER,1,MARKER_LEN,f);
    uint32_t version_be = htobe32(FILE_VERSION);
    fwrite(&version_be, sizeof(version_be), 1, f);

    uint16_t up_stairs_count = d.upCount>0 ? 1 : 0;
    uint16_t down_stairs_count = d.downCount>0 ? 1 : 0;
    uint16_t alive_monsters = 0;
    for(auto c : d.characters){
        if(c->type == Character::NPC_TYPE && c->alive) {
            alive_monsters++;
        }
    }

    uint32_t file_size = 1702 + (d.room_count*4) + 2 + (up_stairs_count*2) + 2 + (down_stairs_count*2);
    file_size += 2 + alive_monsters*5;
    uint32_t file_size_be = htobe32(file_size);
    fwrite(&file_size_be, sizeof(file_size_be), 1, f);

    uint8_t pcx = (uint8_t)d.pc_x;
    uint8_t pcy = (uint8_t)d.pc_y;
    fwrite(&pcx,1,1,f);
    fwrite(&pcy,1,1,f);

    for(int r=0; r<HEIGHT; r++){
        for(int c=0; c<WIDTH; c++){
            uint8_t hh = (uint8_t)d.hardness[r][c];
            fwrite(&hh,1,1,f);
        }
    }

    uint16_t r_be = htobe16(d.room_count);
    fwrite(&r_be, sizeof(r_be), 1, f);
    for(int i=0; i<d.room_count; i++){
        uint8_t rx = (uint8_t)d.rooms[i].x;
        uint8_t ry = (uint8_t)d.rooms[i].y;
        uint8_t rw = (uint8_t)d.rooms[i].w;
        uint8_t rh = (uint8_t)d.rooms[i].h;
        fwrite(&rx,1,1,f);
        fwrite(&ry,1,1,f);
        fwrite(&rw,1,1,f);
        fwrite(&rh,1,1,f);
    }

    uint16_t up_be = htobe16(up_stairs_count);
    fwrite(&up_be, sizeof(up_be), 1, f);
    if(up_stairs_count==1){
        uint8_t sx=(uint8_t)d.up_xCoord, sy=(uint8_t)d.up_yCoord;
        fwrite(&sx,1,1,f);
        fwrite(&sy,1,1,f);
    }
    uint16_t down_be = htobe16(down_stairs_count);
    fwrite(&down_be, sizeof(down_be), 1, f);
    if(down_stairs_count==1){
        uint8_t sx=(uint8_t)d.down_xCoord, sy=(uint8_t)d.down_yCoord;
        fwrite(&sx,1,1,f);
        fwrite(&sy,1,1,f);
    }
    uint16_t am_be = htobe16(alive_monsters);
    fwrite(&am_be, sizeof(am_be), 1, f);
    for(auto c : d.characters){
        if(c->type == Character::NPC_TYPE && c->alive){
            uint8_t mx = (uint8_t)c->x;
            uint8_t my = (uint8_t)c->y;
            uint8_t mspeed = (uint8_t)c->speed;
            uint8_t mhp = (uint8_t)c->hp;
            uint8_t mbtype = (uint8_t)c->btype;
            fwrite(&mx,1,1,f);
            fwrite(&my,1,1,f);
            fwrite(&mspeed,1,1,f);
            fwrite(&mhp,1,1,f);
            fwrite(&mbtype,1,1,f);
        }
    }

    fclose(f);
}

static void load_dungeon(Dungeon &d, const char* path) {
    FILE *f = fopen(path,"rb");
    if(!f){
        std::cerr << "Error opening " << path << " for read\n";
        return;
    }
    char marker[MARKER_LEN+1];
    fread(marker,1,MARKER_LEN,f);
    marker[MARKER_LEN]='\0';
    if(strcmp(marker, FILE_MARKER)!=0){
        std::cerr << "Invalid marker in file\n";
        fclose(f);
        return;
    }
    uint32_t version;
    fread(&version,sizeof(version),1,f);
    version = be32toh(version);
    if(version != FILE_VERSION){
        std::cerr << "Unsupported file version\n";
        fclose(f);
        return;
    }
    uint32_t file_size;
    fread(&file_size, sizeof(file_size),1,f);
    file_size = be32toh(file_size);

    uint8_t pcx, pcy;
    fread(&pcx,1,1,f);
    fread(&pcy,1,1,f);
    d.pc_x = pcx;
    d.pc_y = pcy;

    for(int r=0; r<HEIGHT; r++){
        for(int c=0; c<WIDTH; c++){
            uint8_t h;
            fread(&h,1,1,f);
            d.hardness[r][c] = h;
        }
    }
    uint16_t r_count;
    fread(&r_count, sizeof(r_count),1,f);
    r_count = be16toh(r_count);
    d.room_count = 0;
    for(int i=0; i<r_count && i<MAX_ROOMS; i++){
        uint8_t rx, ry, rw, rh;
        fread(&rx,1,1,f);
        fread(&ry,1,1,f);
        fread(&rw,1,1,f);
        fread(&rh,1,1,f);
        d.rooms[i].x = rx; d.rooms[i].y = ry;
        d.rooms[i].w = rw; d.rooms[i].h = rh;
        d.room_count++;
    }

    uint16_t up_c;
    fread(&up_c, sizeof(up_c),1,f);
    up_c = be16toh(up_c);
    d.upCount = 0;
    if(up_c>0){
        uint8_t sx, sy;
        fread(&sx,1,1,f);
        fread(&sy,1,1,f);
        d.upCount=1; d.up_xCoord=sx; d.up_yCoord=sy;
    }

    uint16_t down_c;
    fread(&down_c, sizeof(down_c),1,f);
    down_c = be16toh(down_c);
    d.downCount=0;
    if(down_c>0){
        uint8_t sx, sy;
        fread(&sx,1,1,f);
        fread(&sy,1,1,f);
        d.downCount=1; d.down_xCoord=sx; d.down_yCoord=sy;
    }
    for(int yy=0; yy<HEIGHT; yy++){
        for(int xx=0; xx<WIDTH; xx++){
            if(d.hardness[yy][xx] == 255) {
                d.base_map[yy][xx] = ' ';
            } else if(d.hardness[yy][xx] > 0) {
                d.base_map[yy][xx] = ' ';
            } else {
                d.base_map[yy][xx] = '#';
            }
        }
    }
    for(int i=0; i<d.room_count; i++){
        for(int row = d.rooms[i].y; row < d.rooms[i].y + d.rooms[i].h; row++){
            for(int col = d.rooms[i].x; col < d.rooms[i].x + d.rooms[i].w; col++){
                d.base_map[row][col] = '.';
            }
        }
    }
    if(d.upCount>0) {
        d.base_map[d.up_yCoord][d.up_xCoord] = '<';
    }
    if(d.downCount>0) {
        d.base_map[d.down_yCoord][d.down_xCoord] = '>';
    }

    uint16_t monster_count;
    size_t ret = fread(&monster_count, sizeof(monster_count),1,f);
    if(ret == 1){
        monster_count = be16toh(monster_count);
    } else {
        monster_count = 0;
    }

    for(auto c : d.characters) {
        delete c;
    }
    d.characters.clear();
    for(int i=0; i<(int)monster_count; i++){
        uint8_t mx,my,mspeed,mhp,mbtype;
        fread(&mx,1,1,f);
        fread(&my,1,1,f);
        fread(&mspeed,1,1,f);
        fread(&mhp,1,1,f);
        fread(&mbtype,1,1,f);
        NPC *mm = new NPC(mbtype, mx, my, mspeed, mhp);
        d.characters.push_back(mm);
    }

    fclose(f);
}
int main(int argc, char *argv[]) {
    srand(time(NULL));
    Dungeon dungeon;
    bool do_load = false;
    bool do_save = false;
    int local_num_mon = DEFAULT_NUMMON;
    for(int i=1; i<argc; i++){
        if(!strcmp(argv[i], "--load")) {
            do_load = true;
        } else if(!strcmp(argv[i], "--save")) {
            do_save = true;
        } else if(!strcmp(argv[i],"--nummon") && i+1<argc) {
            local_num_mon = atoi(argv[++i]);
        }
    }
    dungeon.global_num_monsters = local_num_mon;

    checkDir();
    char path[1024];
    getPath(path, sizeof(path));

    if(do_load){
        load_dungeon(dungeon, path);
    } else {
        // generate random dungeon
        for(int y=0; y<HEIGHT; y++){
            for(int x=0; x<WIDTH; x++){
                if(x==0 || x==WIDTH-1 || y==0 || y==HEIGHT-1){
                    dungeon.hardness[y][x] = 255;
                    dungeon.base_map[y][x]  = ' ';
                } else {
                    dungeon.hardness[y][x] = (rand()%254)+1;
                    dungeon.base_map[y][x]  = ' ';
                }
            }
        }
        generateRooms(dungeon);
        connectRoomsViaCorridor(dungeon);
        placeStairs(dungeon);
        if(dungeon.room_count > 0) {
            dungeon.pc_x = dungeon.rooms[0].x;
            dungeon.pc_y = dungeon.rooms[0].y;
        } else {
            dungeon.pc_x = 1;
            dungeon.pc_y = 1;
        }
    }
    memcpy(dungeon.dungeon, dungeon.base_map, sizeof(dungeon.dungeon));
    dungeon.createPC(dungeon.pc_x, dungeon.pc_y);
    if(!do_load){
        for(int i=0; i<local_num_mon; i++){
            dungeon.createMonster();
        }
    }
    if(do_save){
        save_dungeon(dungeon, path);
    }
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    start_color();
    while(dungeon.pc_is_alive) {
        dungeon.gameLoop();
        if(!dungeon.pc_is_alive) {
            break;
        }
        if(dungeon.changedFloor) {
            dungeon.newLevel(dungeon.global_num_monsters);
        } else {
            break;
        }
    }
    dungeon.rebuildDisplay();
    clear();
    if(!dungeon.pc_is_alive) {
        printw("You lose! The PC has been killed.\n");
    } else {
    }
    printw("Press any key to quit...");
    refresh();
    getch();
    endwin();

    return 0;
}
