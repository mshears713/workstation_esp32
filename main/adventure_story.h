#pragma once
#include <stdint.h>
#include <stdbool.h>
/* Pure, bounded, side-effect-free story model. No audio/network dependencies. */
typedef enum { B_HOME, B_EGYPT, B_GREECE, B_FRANCE, B_PORTAL } b_place_t;
typedef enum { B_CAPTION, B_SPEECH, B_THOUGHT, B_SHOUT } b_text_t;
typedef enum { B_EMERGE, B_INTRO, B_SING, B_BOARD, B_DRIVE, B_WARP, B_STOP, B_FRIEND, B_CLIMB, B_LOOK, B_END } b_action_t;
typedef struct { const char *title,*text; uint32_t duration; b_place_t place; b_text_t treatment; b_action_t action; } b_beat_t;
static const b_beat_t b_story[] = {
 {"A tiny explorer", "Hello! I'm Mouse.\nBig buildings, tiny paws!",12000,B_HOME,B_SPEECH,B_EMERGE},
 {"Mouse at the microphone", "A trip through time!\nNo talking - just tap.",14000,B_HOME,B_SPEECH,B_INTRO},
 {"A silent little song", "Three places. Big ideas.\nLet's look up!",12000,B_HOME,B_THOUGHT,B_SING},
 {"All aboard", "My van is ready.\nNext stop: Egypt!",12000,B_HOME,B_CAPTION,B_BOARD},
 {"Through the time portal", "Make-believe travel.\nReal buildings!",10000,B_PORTAL,B_SHOUT,B_WARP},
 {"EGYPT / Giza", "Sand, sunshine...\nand a very big pyramid!",7000,B_EGYPT,B_CAPTION,B_DRIVE},
 {"A pyramid for a king", "The Great Pyramid was\nbuilt as King Khufu's tomb.",10000,B_EGYPT,B_THOUGHT,B_STOP},
 {"Stone upon stone", "It is made mostly of limestone.\nIt once wore smooth outer stones.",10000,B_EGYPT,B_CAPTION,B_LOOK},
 {"A make-believe friend", "A friendly mummy says hello.\n'Nice van, tiny traveller!'",9000,B_EGYPT,B_SPEECH,B_FRIEND},
 {"Off to ancient Greece", "Goodbye, sandy skyline!",5000,B_EGYPT,B_SHOUT,B_DRIVE},
 {"GREECE / Athens", "Up on the Acropolis...\na temple with rows of columns!",7000,B_GREECE,B_CAPTION,B_DRIVE},
 {"Meet the Parthenon", "The Parthenon stands\non the Acropolis in Athens.",10000,B_GREECE,B_THOUGHT,B_STOP},
 {"Look at those columns", "Its outer columns are Doric.\nSpot their simple tops!",10000,B_GREECE,B_SPEECH,B_CLIMB},
 {"A column-counting song", "Tall columns, standing in a row.\nA silent song before we go!",7000,B_GREECE,B_SHOUT,B_SING},
 {"Next: medieval France", "Time van, take us forward!",5000,B_GREECE,B_CAPTION,B_DRIVE},
 {"FRANCE / Gothic", "Stone towers rise above town.\nLook for the pointed arches!",7000,B_FRANCE,B_CAPTION,B_DRIVE},
 {"A Gothic cathedral", "Gothic arches rise to a point.\nLook for their tall stone curves.",10000,B_FRANCE,B_THOUGHT,B_STOP},
 {"Light becomes colour", "Stained-glass windows turn\nsunlight into glowing colours.",10000,B_FRANCE,B_SPEECH,B_CLIMB},
 {"One last look", "Stone, columns, coloured light...\nWhat a world to explore!",7000,B_FRANCE,B_SHOUT,B_LOOK},
 {"Home through the portal", "Our time trip is complete.\nHome for a well-earned rest!",6000,B_PORTAL,B_CAPTION,B_WARP},
 {"Home, sweet mouse house", "Three places. Big ideas.\nThanks for travelling!",8000,B_HOME,B_SPEECH,B_DRIVE},
 {"The end / Adventure B", "What a big adventure!\nShall we go again?",0,B_HOME,B_CAPTION,B_END}
};
#define B_BEAT_COUNT ((unsigned)(sizeof(b_story)/sizeof(b_story[0])))
typedef struct { unsigned beat; uint32_t elapsed; bool active; uint32_t generation; } b_story_state_t;
static inline void b_story_start(b_story_state_t *s) {s->beat=0;s->elapsed=0;s->active=true;s->generation++;}
static inline void b_story_close(b_story_state_t *s) {s->active=false;s->elapsed=0;s->generation++;}
static inline void b_story_next(b_story_state_t *s) {if(s->active && s->beat+1<B_BEAT_COUNT){s->beat++;s->elapsed=0;}}
static inline void b_story_skip(b_story_state_t *s) {
 if(!s->active)return;
 unsigned to=s->beat<5?5:s->beat<10?10:s->beat<15?15:s->beat<19?19:21;
 s->beat=to;s->elapsed=0;
}
static inline void b_story_advance(b_story_state_t *s,uint32_t delta) {
 if(!s->active)return;
 s->elapsed+=delta;
 while(b_story[s->beat].duration && s->elapsed>=b_story[s->beat].duration){
  s->elapsed-=b_story[s->beat].duration;
  if(s->beat+1<B_BEAT_COUNT)s->beat++;else{s->elapsed=0;break;}
 }
}
