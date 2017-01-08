/*****************************************************************************
                              Blackjack Game Code
                       Copyright (C) Andrew Collington
                amnuts@talker.com     http://amnuts.talker.com/
                       Last update: 21st January, 2000
 *****************************************************************************/


/* game definitions */

#define DEFAULT_BJ_BET   10
#define USE_MONEY_SYSTEM  0

/* game structures */

struct blackjack_game_struct {
  short int deck[52],hand[5],dealer_hand[5],bet,cardpos;
};
typedef struct blackjack_game_struct *BJ_GAME;

char *cards[53][5]={
  { ".-----. ","|~OLA~RS    | ","|  s  | ","|    ~OLA~RS| ","`-----' " },
  { ".-----. ","|~OL2~RS    | ","|  s  | ","|    ~OL2~RS| ","`-----' " },
  { ".-----. ","|~OL3~RS    | ","|  s  | ","|    ~OL3~RS| ","`-----' " },
  { ".-----. ","|~OL4~RS    | ","|  s  | ","|    ~OL4~RS| ","`-----' " },
  { ".-----. ","|~OL5~RS    | ","|  s  | ","|    ~OL5~RS| ","`-----' " },
  { ".-----. ","|~OL6~RS    | ","|  s  | ","|    ~OL6~RS| ","`-----' " },
  { ".-----. ","|~OL7~RS    | ","|  s  | ","|    ~OL7~RS| ","`-----' " },
  { ".-----. ","|~OL8~RS    | ","|  s  | ","|    ~OL8~RS| ","`-----' " },
  { ".-----. ","|~OL9~RS    | ","|  s  | ","|    ~OL9~RS| ","`-----' " },
  { ".-----. ","|~OL10~RS   | ","|  s  | ","|   ~OL10~RS| ","`-----' " },
  { ".-----. ","|~OLJ~RS    | ","|  s  | ","|    ~OLJ~RS| ","`-----' " },
  { ".-----. ","|~OLQ~RS    | ","|  s  | ","|    ~OLQ~RS| ","`-----' " },
  { ".-----. ","|~OLK~RS    | ","|  s  | ","|    ~OLK~RS| ","`-----' " },
  { ".-----. ","|~OLA~RS    | ","|  ~FR~OLd~RS  | ","|    ~OLA~RS| ","`-----' " },
  { ".-----. ","|~OL2~RS    | ","|  ~FR~OLd~RS  | ","|    ~OL2~RS| ","`-----' " },
  { ".-----. ","|~OL3~RS    | ","|  ~FR~OLd~RS  | ","|    ~OL3~RS| ","`-----' " },
  { ".-----. ","|~OL4~RS    | ","|  ~FR~OLd~RS  | ","|    ~OL4~RS| ","`-----' " },
  { ".-----. ","|~OL5~RS    | ","|  ~FR~OLd~RS  | ","|    ~OL5~RS| ","`-----' " },
  { ".-----. ","|~OL6~RS    | ","|  ~FR~OLd~RS  | ","|    ~OL6~RS| ","`-----' " },
  { ".-----. ","|~OL7~RS    | ","|  ~FR~OLd~RS  | ","|    ~OL7~RS| ","`-----' " },
  { ".-----. ","|~OL8~RS    | ","|  ~FR~OLd~RS  | ","|    ~OL8~RS| ","`-----' " },
  { ".-----. ","|~OL9~RS    | ","|  ~FR~OLd~RS  | ","|    ~OL9~RS| ","`-----' " },
  { ".-----. ","|~OL10~RS   | ","|  ~FR~OLd~RS  | ","|   ~OL10~RS| ","`-----' " },
  { ".-----. ","|~OLJ~RS    | ","|  ~FR~OLd~RS  | ","|    ~OLJ~RS| ","`-----' " },
  { ".-----. ","|~OLQ~RS    | ","|  ~FR~OLd~RS  | ","|    ~OLQ~RS| ","`-----' " },
  { ".-----. ","|~OLK~RS    | ","|  ~FR~OLd~RS  | ","|    ~OLK~RS| ","`-----' " },
  { ".-----. ","|~OLA~RS    | ","|  c  | ","|    ~OLA~RS| ","`-----' " },
  { ".-----. ","|~OL2~RS    | ","|  c  | ","|    ~OL2~RS| ","`-----' " },
  { ".-----. ","|~OL3~RS    | ","|  c  | ","|    ~OL3~RS| ","`-----' " },
  { ".-----. ","|~OL4~RS    | ","|  c  | ","|    ~OL4~RS| ","`-----' " },
  { ".-----. ","|~OL5~RS    | ","|  c  | ","|    ~OL5~RS| ","`-----' " },
  { ".-----. ","|~OL6~RS    | ","|  c  | ","|    ~OL6~RS| ","`-----' " },
  { ".-----. ","|~OL7~RS    | ","|  c  | ","|    ~OL7~RS| ","`-----' " },
  { ".-----. ","|~OL8~RS    | ","|  c  | ","|    ~OL8~RS| ","`-----' " },
  { ".-----. ","|~OL9~RS    | ","|  c  | ","|    ~OL9~RS| ","`-----' " },
  { ".-----. ","|~OL10~RS   | ","|  c  | ","|   ~OL10~RS| ","`-----' " },
  { ".-----. ","|~OLJ~RS    | ","|  c  | ","|    ~OLJ~RS| ","`-----' " },
  { ".-----. ","|~OLQ~RS    | ","|  c  | ","|    ~OLQ~RS| ","`-----' " },
  { ".-----. ","|~OLK~RS    | ","|  c  | ","|    ~OLK~RS| ","`-----' " },
  { ".-----. ","|~OLA~RS    | ","|  ~FR~OLh~RS  | ","|    ~OLA~RS| ","`-----' " },
  { ".-----. ","|~OL2~RS    | ","|  ~FR~OLh~RS  | ","|    ~OL2~RS| ","`-----' " },
  { ".-----. ","|~OL3~RS    | ","|  ~FR~OLh~RS  | ","|    ~OL3~RS| ","`-----' " },
  { ".-----. ","|~OL4~RS    | ","|  ~FR~OLh~RS  | ","|    ~OL4~RS| ","`-----' " },
  { ".-----. ","|~OL5~RS    | ","|  ~FR~OLh~RS  | ","|    ~OL5~RS| ","`-----' " },
  { ".-----. ","|~OL6~RS    | ","|  ~FR~OLh~RS  | ","|    ~OL6~RS| ","`-----' " },
  { ".-----. ","|~OL7~RS    | ","|  ~FR~OLh~RS  | ","|    ~OL7~RS| ","`-----' " },
  { ".-----. ","|~OL8~RS    | ","|  ~FR~OLh~RS  | ","|    ~OL8~RS| ","`-----' " },
  { ".-----. ","|~OL9~RS    | ","|  ~FR~OLh~RS  | ","|    ~OL9~RS| ","`-----' " },
  { ".-----. ","|~OL10~RS   | ","|  ~FR~OLh~RS  | ","|   ~OL10~RS| ","`-----' " },
  { ".-----. ","|~OLJ~RS    | ","|  ~FR~OLh~RS  | ","|    ~OLJ~RS| ","`-----' " },
  { ".-----. ","|~OLQ~RS    | ","|  ~FR~OLh~RS  | ","|    ~OLQ~RS| ","`-----' " },
  { ".-----. ","|~OLK~RS    | ","|  ~FR~OLh~RS  | ","|    ~OLK~RS| ","`-----' " },
  { ".-----. ","|~FRO~FGX~FRO~FGX~FRO~RS| ","|~FGX~FRO~FGX~FRO~FGX~RS| ","|~FRO~FGX~FRO~FGX~FRO~RS| ","`-----' " }
};

/* prototypes */

BJ_GAME   create_blackjack_game(void);
void      destruct_blackjack_game(UR_OBJECT);
void      play_blackjack(UR_OBJECT);
void      show_blackjack_cards(UR_OBJECT,int,int);
int       check_blackjack_total(UR_OBJECT,int);
