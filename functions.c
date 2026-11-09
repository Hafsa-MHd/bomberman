#define _CRT_SECURE_NO_WARNINGS
#include "functions.h"
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* timings/config */
static const int TICK_MS=50, BOMB_TIMER_MS=2000, BOMB_RANGE=2;
static const int GOJO_DURATION_MS=3000, GOJO_COOLDOWN_MS=10000;
static const int TOJI_DASH_RANGE=3, TOJI_COOLDOWN_MS=4000;
static const int EXPLOSION_FLASH_MS=200;

/* toggles */
static int g_assist_place=0;  /* [F] place under feet if front blocked */
static int g_show_help=1;     /* [H] show help */
static int g_show_keydbg=0;   /* [K] show last key code */
static int g_last_key=-1;

static char g_msg[120]; static unsigned long g_msg_until=0;

/* utils */
static unsigned long now_ms(void){ return (unsigned long)GetTickCount64(); }
static void set_cursor(short r, short c){ COORD cc={c,r}; SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE),cc); }
static void hide_cursor(bool hide){ HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE); CONSOLE_CURSOR_INFO ci; GetConsoleCursorInfo(h,&ci); ci.bVisible=hide?FALSE:TRUE; SetConsoleCursorInfo(h,&ci); }
static void show_msg_ms(const char*m,unsigned long d){ strncpy(g_msg,m,sizeof g_msg-1); g_msg[sizeof g_msg-1]='\0'; g_msg_until=now_ms()+d; }

/* arena load */
static int load_arena_from_file(const char*path,Arena*a){
    FILE*f=fopen(path,"r"); if(!f) return 0;
    char line[1024]; int h=0,w=0;
    while(fgets(line,sizeof line,f) && h<MAX_H){
        int len=(int)strcspn(line,"\r\n"); if(len>w) w=len; if(len>MAX_W) len=MAX_W;
        for(int x=0;x<len;++x){ char c=line[x]; a->grid[h][x]=(c=='#')?CELL_WALL:(c=='X'?CELL_OBS:CELL_EMPTY); }
        for(int x=len;x<MAX_W;++x) a->grid[h][x]=CELL_EMPTY;
        ++h;
    }
    fclose(f);
    if(h==0||w==0) return 0;
    a->h=h; a->w=w; return 1;
}
static int is_inside(const Arena*a,int y,int x){ return y>=0&&y<a->h&&x>=0&&x<a->w; }
static int is_walkable(const Arena*a,int y,int x){ return is_inside(a,y,x) && a->grid[y][x]!=CELL_WALL && a->grid[y][x]!=CELL_OBS; }
static int count_obstacles(const Arena*a){ int c=0; for(int y=0;y<a->h;++y) for(int x=0;x<a->w;++x) if(a->grid[y][x]==CELL_OBS) c++; return c; }
static int random_empty_cell(const Arena*a,int*oy,int*ox){
    for(int t=0;t<a->h*a->w*3;++t){ int y=rand()%a->h, x=rand()%a->w; if(a->grid[y][x]==CELL_EMPTY){ *oy=y; *ox=x; return 1; } }
    for(int y=0;y<a->h;++y) for(int x=0;x<a->w;++x) if(a->grid[y][x]==CELL_EMPTY){ *oy=y; *ox=x; return 1; }
    return 0;
}

/* players */
static void init_player(Player*p){ memset(p,0,sizeof* p); p->alive=true; p->bomb.ownerId=-1; p->ability.lastUsedMs=-1000000; }
static void place_players_random(const Arena*a,Player ps[2]){
    int y1,x1,y2,x2;
    while(1){
        if(!random_empty_cell(a,&y1,&x1)) continue;
        if(!random_empty_cell(a,&y2,&x2)) continue;
        if(y1==y2 && x1==x2) continue;
        ps[0].pos.y=y1; ps[0].pos.x=x1; ps[1].pos.y=y2; ps[1].pos.x=x2; return;
    }
}

/* mechanics */
static int in_blast_line(const Arena*a, Pos c, Pos p, int range){
    if(p.y==c.y && p.x==c.x) return 1;
    if(p.x==c.x){
        int dy=(p.y<c.y)?-1:1; int steps=(p.y-c.y)*dy; if(steps>range) return 0;
        for(int y=c.y+dy; y!=p.y+dy; y+=dy) if(a->grid[y][p.x]==CELL_WALL) return 0; return 1;
    }
    if(p.y==c.y){
        int dx=(p.x<c.x)?-1:1; int steps=(p.x-c.x)*dx; if(steps>range) return 0;
        for(int x=c.x+dx; x!=p.x+dx; x+=dx) if(a->grid[p.y][x]==CELL_WALL) return 0; return 1;
    }
    return 0;
}
static void try_move_player(const Arena*a, Player*p, int dy,int dx, const Player*o){
    int ny=p->pos.y+dy, nx=p->pos.x+dx;
    if(!is_walkable(a,ny,nx)) return;
    if(o->alive && o->pos.y==ny && o->pos.x==nx) return;
    p->pos.y=ny; p->pos.x=nx;
}
static void arm_bomb(Player*p,const Arena*a,int dy,int dx,int owner){
    if(p->bomb.active){ show_msg_ms("Bomb already active",900); return; }
    Pos target=(Pos){p->pos.y+dy,p->pos.x+dx}; int ok=0;
    if(is_inside(a,target.y,target.x) && a->grid[target.y][target.x]==CELL_EMPTY) ok=1;
    else if(g_assist_place && a->grid[p->pos.y][p->pos.x]==CELL_EMPTY){ target=p->pos; ok=1; } /* assist */
    if(!ok){ show_msg_ms("Front blocked",800); return; }
    unsigned long t=now_ms();
    p->bomb.active=true; p->bomb.ownerId=owner; p->bomb.pos=target;
    p->bomb.armedAt=t; p->bomb.explodeAt=t+BOMB_TIMER_MS; p->bomb.explodingUntil=0; p->bomb.range=BOMB_RANGE;
    p->bombsPlaced++;
}
static void apply_explosion(Arena*a, Player ps[2], Bomb*b){
    int destroyed=0;
    if(a->grid[b->pos.y][b->pos.x]==CELL_OBS){ a->grid[b->pos.y][b->pos.x]=CELL_EMPTY; destroyed++; }
    for(int d=1;d<=b->range;++d){ int y=b->pos.y-d,x=b->pos.x; if(!is_inside(a,y,x)||a->grid[y][x]==CELL_WALL) break; if(a->grid[y][x]==CELL_OBS){a->grid[y][x]=CELL_EMPTY; destroyed++;} }
    for(int d=1;d<=b->range;++d){ int y=b->pos.y+d,x=b->pos.x; if(!is_inside(a,y,x)||a->grid[y][x]==CELL_WALL) break; if(a->grid[y][x]==CELL_OBS){a->grid[y][x]=CELL_EMPTY; destroyed++;} }
    for(int d=1;d<=b->range;++d){ int y=b->pos.y,x=b->pos.x-d; if(!is_inside(a,y,x)||a->grid[y][x]==CELL_WALL) break; if(a->grid[y][x]==CELL_OBS){a->grid[y][x]=CELL_EMPTY; destroyed++;} }
    for(int d=1;d<=b->range;++d){ int y=b->pos.y,x=b->pos.x+d; if(!is_inside(a,y,x)||a->grid[y][x]==CELL_WALL) break; if(a->grid[y][x]==CELL_OBS){a->grid[y][x]=CELL_EMPTY; destroyed++;} }
    if(b->ownerId>=0 && b->ownerId<2) ps[b->ownerId].obstaclesDestroyed += destroyed;

    for(int i=0;i<2;++i){
        if(!ps[i].alive) continue;
        if(!in_blast_line(a,b->pos,ps[i].pos,b->range)) continue;
        unsigned long t=now_ms();
        int gojo=(ps[i].kind==CHAR_GOJO) && (ps[i].ability.activeUntil>t);
        if(!gojo) ps[i].alive=false; /* why: explosion kills unless Gojo barrier active */
    }
}

/* UI */
static void draw_help(int row){
    set_cursor((short)row,0);
    printf("[H] help  [F] assist:%s  [K] keydbg:%s\n",
           g_assist_place?"ON":"OFF", g_show_keydbg?"ON":"OFF");
    printf("P1: ZQSD (move), W (bomb), A (ability) | P2: Arrows, Space (bomb), P (ability) | ESC quit\n");
}
static void draw_debug(int row, const Player ps[2]){
    set_cursor((short)row,0);
    printf("P1(y=%d,x=%d,alive=%d)  P2(y=%d,x=%d,alive=%d)  lastKey:%d                \n",
           ps[0].pos.y, ps[0].pos.x, ps[0].alive?1:0,
           ps[1].pos.y, ps[1].pos.x, ps[1].alive?1:0,
           g_show_keydbg?g_last_key:-1);
}
static void draw(const Arena*a,const Player ps[2],const Bomb bs[2], unsigned long t){
    set_cursor(0,0);
    for(int y=0;y<a->h;++y){
        for(int x=0;x<a->w;++x){
            char ch=' ';
            if(a->grid[y][x]==CELL_WALL) ch='#';
            else if(a->grid[y][x]==CELL_OBS) ch='X';
            putchar(ch);
        }
        putchar('\n');
    }
    for(int i=0;i<2;++i){
        const Bomb*b=&bs[i]; if(!b->active) continue;
        if(t<b->explodeAt){
            if(b->pos.y<a->h && b->pos.x<a->w){ set_cursor((short)b->pos.y,(short)b->pos.x); putchar('o'); }
        }else if(b->explodingUntil && t<=b->explodingUntil){
            set_cursor((short)b->pos.y,(short)b->pos.x); putchar('*');
            for(int d=1;d<=b->range;++d){
                if(b->pos.y-d>=0){ set_cursor((short)(b->pos.y-d),(short)b->pos.x); putchar('*'); }
                if(b->pos.y+d<a->h){ set_cursor((short)(b->pos.y+d),(short)b->pos.x); putchar('*'); }
                if(b->pos.x-d>=0){ set_cursor((short)b->pos.y,(short)(b->pos.x-d)); putchar('*'); }
                if(b->pos.x+d<a->w){ set_cursor((short)b->pos.y,(short)(b->pos.x+d)); putchar('*'); }
            }
        }
    }
    /* render players as G/T (requested) */
    if(ps[0].alive){ set_cursor((short)ps[0].pos.y,(short)ps[0].pos.x); putchar(ps[0].kind==CHAR_GOJO?'G':'T'); }
    if(ps[1].alive){ set_cursor((short)ps[1].pos.y,(short)ps[1].pos.x); putchar(ps[1].kind==CHAR_GOJO?'G':'T'); }

    int hud=a->h+1; set_cursor((short)hud,0);
    int s1=(ps[0].obstaclesDestroyed*50)/(ps[0].bombsPlaced?ps[0].bombsPlaced:1);
    int s2=(ps[1].obstaclesDestroyed*50)/(ps[1].bombsPlaced?ps[1].bombsPlaced:1);
    unsigned long now=t; long cd1=0, cd2=0;
    if((long)(ps[0].ability.lastUsedMs+ps[0].ability.cooldownMs)>(long)now)
        cd1=(long)((ps[0].ability.lastUsedMs+ps[0].ability.cooldownMs)-now+999)/1000;
    if((long)(ps[1].ability.lastUsedMs+ps[1].ability.cooldownMs)>(long)now)
        cd2=(long)((ps[1].ability.lastUsedMs+ps[1].ability.cooldownMs)-now+999)/1000;

    printf("P1[%s|%s] score:%d (X:%d,B:%d,CD:%lds) | P2[%s|%s] score:%d (X:%d,B:%d,CD:%lds) | Obstacles:%d\n",
           ps[0].name, ps[0].kind==CHAR_GOJO?"Gojo":"Toji", s1, ps[0].obstaclesDestroyed, ps[0].bombsPlaced, cd1,
           ps[1].name, ps[1].kind==CHAR_GOJO?"Gojo":"Toji", s2, ps[1].obstaclesDestroyed, ps[1].bombsPlaced, cd2,
           count_obstacles(a));

    set_cursor((short)(hud+1),0);
    if(g_msg_until && now<=g_msg_until) printf("%s\n", g_msg);
    else { printf(" \n"); g_msg_until=0; }

    if(g_show_help) draw_help(hud+2);
    draw_debug(hud + (g_show_help?4:2), ps);
}

/* highscores */
static void today_str(char out[20]){ time_t tt=time(NULL); struct tm*tm=localtime(&tt); strftime(out,20,"%Y-%m-%d",tm); }
static void save_scores(const char*n1,int s1,const char*n2,int s2){
    FILE*f=fopen("highscores.csv","a"); if(!f){ f=fopen("highscores.csv","w"); if(!f) return; }
    char d[20]; today_str(d); fprintf(f,"%s,%d,%s\n",n1,s1,d); fprintf(f,"%s,%d,%s\n",n2,s2,d); fclose(f);
}

/* console helpers */
static void disable_quick_edit(void){
    HANDLE hIn=GetStdHandle(STD_INPUT_HANDLE); DWORD mode=0; if(!GetConsoleMode(hIn,&mode)) return;
    mode &= ~(0x0040); mode |= 0x0080; SetConsoleMode(hIn,mode);
}
static void read_key(int *outCh,int *isArrow){
    *isArrow=0; *outCh=-1;
    if(!_kbhit()) return;
    int c=_getch();
    if(c==0||c==224){ *isArrow=1; *outCh=_getch(); } else { *outCh=c; }
    g_last_key=*outCh;
}
static void read_line(const char *label, char out[MAX_NAME]){
    printf("%s (max %d): ",label,MAX_NAME-1); fflush(stdout);
    if(!fgets(out,MAX_NAME,stdin)) strcpy(out,"Player");
    size_t n=strlen(out); if(n&&(out[n-1]=='\n'||out[n-1]=='\r')) out[n-1]='\0';
    if(!out[0]) strcpy(out,"Player");
}
static int read_choice_gojo_toji(const char*title){
    while(1){
        printf("\n%s\n1) Gojo (barrier 3s, CD10s)\n2) Toji (dash 3, CD4s)\nChoose 1/2: ",title); fflush(stdout);
        char b[8]; if(!fgets(b,sizeof b,stdin)) continue;
        if(b[0]=='1') return 1; if(b[0]=='2') return 2;
    }
}

/* main loop */
void run_game(void){
    srand((unsigned)time(NULL));
    system("mode con: cols=120 lines=45");
    disable_quick_edit();
    hide_cursor(true);
    system("cls");

    Arena a; memset(&a,0,sizeof a);
    if(!load_arena_from_file("arena.txt",&a)){
        printf("Error: arena.txt not found (put it next to the .exe)."); _getch(); hide_cursor(false); return;
    }

    Player ps[2]; init_player(&ps[0]); init_player(&ps[1]);

    printf("=== Bomber (G/T view) ===\n");
    read_line("Player1 name",ps[0].name); read_line("Player2 name",ps[1].name);
    int c1=read_choice_gojo_toji("P1 style"); int c2=read_choice_gojo_toji("P2 style");
    ps[0].kind=(c1==1)?CHAR_GOJO:CHAR_TOJI; ps[1].kind=(c2==1)?CHAR_GOJO:CHAR_TOJI;
    ps[0].ability.cooldownMs=(ps[0].kind==CHAR_GOJO)?GOJO_COOLDOWN_MS:TOJI_COOLDOWN_MS;
    ps[1].ability.cooldownMs=(ps[1].kind==CHAR_GOJO)?GOJO_COOLDOWN_MS:TOJI_COOLDOWN_MS;

    int dir1y=0,dir1x=1, dir2y=0,dir2x=-1;
    place_players_random(&a,ps);

    unsigned long last=now_ms();
    system("cls");
    show_msg_ms("Rendering G/T only. [H] help, [F] assist, [K] key debug",2000);

    int running=1;
    while(running){
        unsigned long t=now_ms();
        if(t-last<TICK_MS){ Sleep(5); continue; }
        last=t;

        int key,isA;
        while(_kbhit()){
            read_key(&key,&isA); if(key<0) break;

            if(!isA){
                if(key=='z'||key=='Z'){ try_move_player(&a,&ps[0],-1,0,&ps[1]); dir1y=-1; dir1x=0; }
                else if(key=='s'||key=='S'){ try_move_player(&a,&ps[0], 1,0,&ps[1]); dir1y= 1; dir1x=0; }
                else if(key=='q'||key=='Q'){ try_move_player(&a,&ps[0], 0,-1,&ps[1]); dir1y=0; dir1x=-1; }
                else if(key=='d'||key=='D'){ try_move_player(&a,&ps[0], 0, 1,&ps[1]); dir1y=0; dir1x= 1; }
                else if(key=='w'||key=='W'){ arm_bomb(&ps[0],&a,dir1y,dir1x,0); }
                else if(key=='a'||key=='A'){
                    long ready=(long)ps[0].ability.lastUsedMs+ps[0].ability.cooldownMs <= (long)t;
                    if(ready){
                        ps[0].ability.lastUsedMs=(long)t;
                        if(ps[0].kind==CHAR_GOJO){ ps[0].ability.activeUntil=t+GOJO_DURATION_MS; show_msg_ms("Gojo P1: barrier 3s",900); }
                        else{
                            int ny=ps[0].pos.y,nx=ps[0].pos.x,d=0;
                            for(int s=0;s<TOJI_DASH_RANGE;++s){
                                int ty=ny+dir1y,tx=nx+dir1x; if(!is_inside(&a,ty,tx)||a.grid[ty][tx]==CELL_WALL) break;
                                if(ps[1].alive && ps[1].pos.y==ty && ps[1].pos.x==tx) break;
                                if(a.grid[ty][tx]==CELL_OBS){ a.grid[ty][tx]=CELL_EMPTY; d++; }
                                ny=ty; nx=tx;
                            }
                            ps[0].pos.y=ny; ps[0].pos.x=nx; ps[0].obstaclesDestroyed+=d;
                        }
                    } else show_msg_ms("P1 ability cooldown",700);
                }
                else if(key=='h'||key=='H'){ g_show_help=!g_show_help; }
                else if(key=='f'||key=='F'){ g_assist_place=!g_assist_place; show_msg_ms(g_assist_place?"Assist ON":"Assist OFF",900); }
                else if(key=='k'||key=='K'){ g_show_keydbg=!g_show_keydbg; }
                else if(key==27){ running=0; }
            }else{
                if(key==72){ try_move_player(&a,&ps[1],-1,0,&ps[0]); dir2y=-1; dir2x=0; }
                else if(key==80){ try_move_player(&a,&ps[1], 1,0,&ps[0]);  dir2y= 1; dir2x=0; }
                else if(key==75){ try_move_player(&a,&ps[1], 0,-1,&ps[0]); dir2y=0; dir2x=-1; }
                else if(key==77){ try_move_player(&a,&ps[1], 0, 1,&ps[0]);  dir2y=0; dir2x= 1; }
            }
        }
        if(GetAsyncKeyState(VK_SPACE)&0x8000){ arm_bomb(&ps[1],&a,dir2y,dir2x,1); }
        if((GetAsyncKeyState('P')&0x8000)||(GetAsyncKeyState('p')&0x8000)){
            long ready=(long)ps[1].ability.lastUsedMs+ps[1].ability.cooldownMs <= (long)t;
            if(ready){
                ps[1].ability.lastUsedMs=(long)t;
                if(ps[1].kind==CHAR_GOJO){ ps[1].ability.activeUntil=t+GOJO_DURATION_MS; show_msg_ms("Gojo P2: barrier 3s",900); }
                else{
                    int ny=ps[1].pos.y,nx=ps[1].pos.x,d=0;
                    for(int s=0;s<TOJI_DASH_RANGE;++s){
                        int ty=ny+dir2y,tx=nx+dir2x; if(!is_inside(&a,ty,tx)||a.grid[ty][tx]==CELL_WALL) break;
                        if(ps[0].alive && ps[0].pos.y==ty && ps[0].pos.x==tx) break;
                        if(a.grid[ty][tx]==CELL_OBS){ a.grid[ty][tx]=CELL_EMPTY; d++; }
                        ny=ty; nx=tx;
                    }
                    ps[1].pos.y=ny; ps[1].pos.x=nx; ps[1].obstaclesDestroyed+=d;
                }
            } else show_msg_ms("P2 ability cooldown",700);
        }

        for(int i=0;i<2;++i){
            Bomb*b=&ps[i].bomb; if(!b->active) continue;
            if(b->explodingUntil && t>b->explodingUntil){ b->active=false; b->explodingUntil=0; continue; }
            if(t>=b->explodeAt && !b->explodingUntil){ apply_explosion(&a,ps,b); b->explodingUntil=t+EXPLOSION_FLASH_MS; }
        }

        int rem=count_obstacles(&a); int dead=!(ps[0].alive && ps[1].alive);
        if(rem==0 || dead){
            system("cls");
            int s1=(ps[0].obstaclesDestroyed*50)/(ps[0].bombsPlaced?ps[0].bombsPlaced:1);
            int s2=(ps[1].obstaclesDestroyed*50)/(ps[1].bombsPlaced?ps[1].bombsPlaced:1);
            printf("=== GAME OVER ===\n");
            printf("P1 %s score=%d (X=%d,B=%d) %s\n",ps[0].name,s1,ps[0].obstaclesDestroyed,ps[0].bombsPlaced,ps[0].alive?"ALIVE":"DEAD");
            printf("P2 %s score=%d (X=%d,B=%d) %s\n",ps[1].name,s2,ps[1].obstaclesDestroyed,ps[1].bombsPlaced,ps[1].alive?"ALIVE":"DEAD");
            const char *win="Draw";
            if(dead) win = ps[0].alive ? ps[0].name : (ps[1].alive ? ps[1].name : "None");
            else { if(s1!=s2) win=(s1>s2)?ps[0].name:ps[1].name;
                   else if(ps[0].bombsPlaced!=ps[1].bombsPlaced) win=(ps[0].bombsPlaced<ps[1].bombsPlaced)?ps[0].name:ps[1].name;
                   else win=(strcmp(ps[0].name,ps[1].name)<=0)?ps[0].name:ps[1].name; }
            printf("Winner: %s\n",win);
            FILE*f=fopen("highscores.csv","a"); if(!f){ f=fopen("highscores.csv","w"); if(f) fclose(f); }
            save_scores(ps[0].name,s1,ps[1].name,s2);
            printf("\nSaved highscores.csv\nPress any key to exit..."); _getch();
            hide_cursor(false); return;
        }

        draw(&a,ps,(Bomb[2]){ps[0].bomb,ps[1].bomb},t);
        fflush(stdout);
    }
    hide_cursor(false);
}
