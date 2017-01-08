/*****************************************************************************
                              Blackjack Game Code
                       Copyright (C) Andrew Collington
                amnuts@talker.com     http://amnuts.talker.com/
                       Last update: 21st January, 2000
 *****************************************************************************/


/* create a new blackjack game in memory */
BJ_GAME create_blackjack_game(void)
{
  BJ_GAME bj;
  int i,j,tmp;
  
  if ((bj=(BJ_GAME)malloc(sizeof(struct blackjack_game_struct)))==NULL) {
    write_syslog(SYSLOG,0,"GAMES: Memory allocation failure in create_blackjack_game().\n");
    return NULL;
  }
  /* initialise game structure */
  for (i=0;i<52;i++) bj->deck[i]=i;
  for (i=0;i<5;i++) {
    bj->hand[i]=-1;
    bj->dealer_hand[i]=-1;
  }
  bj->bet=10;
  bj->cardpos=0;
  /* shuffle cards */
  srand(time(0));
  i=j=tmp=0;
  for (i=0;i<52;i++) {
    j=i+(rand()%(52-i));
    tmp=bj->deck[i];
    bj->deck[i]=bj->deck[j];
    bj->deck[j]=tmp;
  }
  /* deal first set of cards */
  bj->hand[0]=bj->deck[bj->cardpos];  ++bj->cardpos;
  bj->dealer_hand[0]=bj->deck[bj->cardpos];  ++bj->cardpos;
  bj->hand[1]=bj->deck[bj->cardpos];  ++bj->cardpos;
  bj->dealer_hand[1]=bj->deck[bj->cardpos];  ++bj->cardpos;
  bj->bet=DEFAULT_BJ_BET;
  return bj;
}


/* destory the malloc'd bjack game */
void destruct_blackjack_game(UR_OBJECT user)
{
  if (user->bj_game==NULL) return;
  free(user->bj_game);
  user->bj_game=NULL;
}


/* lets a user start, stop or check out their status of a game of Blackjack */
void play_blackjack(UR_OBJECT user)
{
  int i,user_total,dealer_total,cnt,blank;

  if (word_count<2) {
#if USE_MONEY_SYSTEM
    write_user(user,"Usage: bjack deal [<ante>]/hit/stand/double/surrender/status\n");
#else
    write_user(user,"Usage: bjack deal/hit/stand/surrender/status\n");
#endif
    return;
  }
  /* start off a blackjack game */
  if (!strcasecmp(word[1],"deal")) {
    if (user->bj_game!=NULL) {
      sprintf(text,"You are already playing a game of Blackjack.\n");
      write_user(user,text);
      return;
    }
    if ((user->bj_game=create_blackjack_game())==NULL) {
      sprintf(text,"You just can't find a pack of cards when ya need 'em!\n");
      write_user(user,text);
      return;
    }
#if USE_MONEY_SYSTEM
    /* did the user bet anything? */
    if (word_count>2) {
      user->bj_game->bet=atoi(word[2]);
      if (!user->bj_game->bet) {
	sprintf(test,"If you're going bet then ante a good amount!\n");
	write_user(user,text);
	destruct_blackjack_game(user);
	return;
      }
    }
    /* continue with trying to make game */
    if (user->money<user->bj_game->bet) {
      sprintf(text,"You haven't got enough money for the $%d ante!\n",user->bj_game->bet);
      write_user(user,text);
      destruct_blackjack_game(user);
      return;
    }
    user->money-=user->bj_game->bet;
    sprintf(text,"~FT~OLThe dealer says:~RS Ante's up!  The table bet is $%d.\n\n",user->bj_game->bet);
    write_user(user,text);
#endif
    sprintf(text,"~FY~OLYour current blackjack hand is...\n");
    write_user(user,text);
    show_blackjack_cards(user,0,1);
    write_user(user,"\n~FM~OLThe dealer's hand is...\n");
    show_blackjack_cards(user,1,1);
    if ((user_total=check_blackjack_total(user,0))==21) {
#if USE_MONEY_SYSTEM
      sprintf(text,"~FT~OLThe dealer says:~RS You've just got ~OLBlackjack~RS, so you win $%d!\n",user->bj_game->bet*3);
      write_user(user,text);
      user->money+=user->bj_game->bet*3;
#else
      write_user(user,"~FT~OLThe dealer says:~RS You've just got ~OLBlackjack~RS!\n");
#endif
      destruct_blackjack_game(user);
    }
    else sprintf(text,"\n~FT~OLThe dealer says:~RS You can now hit, stand%s or surrender.\n\n",(USE_MONEY_SYSTEM)?", double":"");
    write_user(user,text);
    return;
  }
  /* show the status of the game */
  if (!strcasecmp(word[1],"status")) {
    if (user->bj_game==NULL) {
      write_user(user,"You aren't playing a game of Blackjack.\n");
      return;
    }
    write_user(user,"~FY~OLYour current blackjack hand is...\n");
    show_blackjack_cards(user,0,1);
    write_user(user,"\n~FM~OLThe dealer's hand is...\n");
    show_blackjack_cards(user,1,1);
    sprintf(text,"~FT~OLThe dealer says:~RS You can now hit, stand%s or surrender (or see the status).\n\n",(USE_MONEY_SYSTEM)?", double":"");
    write_user(user,text);
    return;
  }
  /* end a game */
  if (!strcmp(word[1],"surrender")) {
    if (user->bj_game==NULL) {
      write_user(user,"You aren't playing a game of Blackjack.\n");
      return;
    }
#if USER_MONEY_SYSTEM
    user->money+=(user->bj_game->bet/2);  /* full bet is taken off to start, so give half back */
    write_user(user,"~FT~OLThe dealer says:~RS Sorry, but you have surrendered and lose $%d - half your bet.\n",user->bj_game->bet/2);
#else
    write_user(user,"~FT~OLThe dealer says:~RS You have surrendered your game.\n");
#endif
    destruct_blackjack_game(user);
    return;
  }
  /* take another card */
  if (!strcasecmp(word[1],"hit")) {
    if (user->bj_game==NULL) {
      write_user(user,"You aren't playing a game of Blackjack.\n");
      return;
    }
    cnt=blank=0;
    for (i=0;i<5;i++) {
      if (user->bj_game->hand[i]==-1) blank++;
      else cnt++;
    }
    if (!blank) {
#if USE_MONEY_SYSTEM
      write_user(user,"~FT~OLThe dealer says:~RS Well done! You got a five card hand and win $%d.\n",
		  ((user->bj_game->bet*2)+(user->bj_game->bet/2)));
      user->money+=((user->bj_game->bet*2)+(user->bj_game->bet/2));
#else
      write_user(user,"~FT~OLThe dealer says:~RS Well done!  You got a five card hand.\n");
#endif
      destruct_blackjack_game(user);
      return;
    }
    user->bj_game->hand[cnt]=user->bj_game->deck[user->bj_game->cardpos];
    ++user->bj_game->cardpos;
    write_user(user,"~FY~OLYour current blackjack hand is...\n");
    show_blackjack_cards(user,0,0);
    user_total=check_blackjack_total(user,0);
    if (user_total>21) {
#if USE_MONEY_SYSTEM
      write_user(user,"~FT~OLThe dealer says:~RS Sorry, but you have busted and lose your bet of $%d.\n",user->bj_game->bet);
#else
      write_user(user,"~FT~OLThe dealer says:~RS Sorry, but you have busted.\n");
#endif
      destruct_blackjack_game(user);
      return;
    }
    if (++cnt>=5) {
#if USE_MONEY_SYSTEM
      write_user(user,"~FT~OLThe dealer says:~RS Well done!  You got a five card hand and win $%d.\n",
		  ((user->bj_game->bet*2)+(user->bj_game->bet/2)));
      user->money+=((user->bj_game->bet*2)+(user->bj_game->bet/2));
#else
      write_user(user,"~FT~OLThe dealer says:~RS Well done!  You have got a five card hand.\n");
#endif
      destruct_blackjack_game(user);
      return;
    }
    sprintf(text,"~FT~OLThe dealer says:~RS You can now hit, stand%s or surrender (or see the status).\n\n",(USE_MONEY_SYSTEM)?", double":"");
    write_user(user,text);
    return;
  }
#if USE_MONEY_SYSTEM
  /* double the bet on the current hand */
  if (!strcasecmp(word[1],"double")) {
    if (user->bj_game==NULL) {
      write_user(user,"You aren't playing a game of Blackjack.\n");
      return;
    }
    if (user->money<user->bj_game->bet) {
      write_user(user,"You can't afford to double your bet.\n");
      return;
    }
    user->money-=user->bj_game->bet;  /* take it off again */
    user->bj_game->bet+=user->bj_game->bet;
    cnt=blank=0;
    for (i=0;i<5;i++) {
      if (user->bj_game->hand[i]==-1) blank++;
      else cnt++;
    }
    if (!blank) {
      write_user(user,"~FT~OLThe dealer says:~RS Well done!  You have got a five card hand and win $%d.\n",
		  ((user->bj_game->bet*2)+(user->bj_game->bet/2)));
      user->money+=((user->bj_game->bet*2)+(user->bj_game->bet/2));
      write_user(user,"~FT~OLThe dealer says:~RS Well done!  You have got a five card hand.\n");
      destruct_blackjack_game(user);
      return;
    }
    user->bj_game->hand[cnt]=user->bj_game->deck[user->bj_game->cardpos];
    ++user->bj_game->cardpos;
    write_user(user,"You double your bet to $%d and draw just one more card...\n",user->bj_game->bet);
    show_blackjack_cards(user,0,0);
    if ((user_total=check_blackjack_total(user,0))>21) {
      write_user(user,"~FT~OLThe dealer says:~RS Sorry, but you busted and lose your bet of $%d.\n",user->bj_game->bet);
      destruct_blackjack_game(user);
      return;
    }
    if (++cnt>=5) {
      write_user(user,"~FT~OLThe dealer says:~RS Well done!  You have got a five card hand and win $%d.\n",
		  ((user->bj_game->bet*2)+(user->bj_game->bet/2)));
      user->money+=((user->bj_game->bet*2)+(user->bj_game->bet/2));
      destruct_blackjack_game(user);
      return;
    }
    write_user(user,"The dealer now takes their turn...\n");
    goto CARD_SKIP;
  }
#endif
  /* stand with the current hand */
  if (!strcasecmp(word[1],"stand")) {
    if (user->bj_game==NULL) {
      write_user(user,"You aren't playing a game of Blackjack.\n");
      return;
    }
    write_user(user,"~FY~OLYou stand, and the dealer takes their turn...\n");
    user_total=check_blackjack_total(user,0);
  CARD_SKIP:
    cnt=blank=0;
    for (i=0;i<5;i++) {
      if (user->bj_game->dealer_hand[i]==-1) blank++;
      else cnt++;
    }
    if (!blank) {
#if USE_MONEY_SYSTEM
      write_user(user,"~FT~OLThe dealer says:~RS I have a five card hand, so you lose $%d.\n",user->bj_game->bet);
#else
      write_user(user,"~FT~OLThe dealer says:~RS I have a five card hand, so you lose.\n");
#endif
      destruct_blackjack_game(user);
      return; 
    }
    dealer_total=check_blackjack_total(user,1);
    while(dealer_total<17) {
      user->bj_game->dealer_hand[cnt]=user->bj_game->deck[user->bj_game->cardpos];
      ++user->bj_game->cardpos;
      ++cnt;
      dealer_total=check_blackjack_total(user,1);
      if (cnt>=5) break;
    }
    write_user(user,"\n~FM~OLThe dealer's hand is...\n");
    show_blackjack_cards(user,1,0);
#if USE_MONEY_SYSTEM
    if (dealer_total>21) {
      write_user(user,"~FT~OLThe dealer says:~RS I've busted so you win $%d.\n",user->bj_game->bet*2);
      user->money+=user->bj_game->bet*2;
    }
    else if (cnt>=5) {
      write_user(user,"~FT~OLThe dealer says:~RS I've got a five card hand, so you lose $%d.\n",user->bj_game->bet);
    }
    else if (dealer_total>user_total) {
      write_user(user,"~FT~OLThe dealer says:~RS I beat your score so you lose $%d.\n",user->bj_game->bet);
    }
    else if (dealer_total<user_total) {
      write_user(user,"~FT~OLThe dealer says:~RS You've beaten me so you win $%d.\n",user->bj_game->bet*2);
      user->money+=user->bj_game->bet*2;
    }
    else {
      write_user(user,"~FT~OLThe dealer says:~RS Push! We've both got the same score so you get back your $%d!\n",user->bj_game->bet);
      user->money+=user->bj_game->bet;
    }
#else
    if (dealer_total>21) {
      write_user(user,"~FT~OLThe dealer says:~RS I've busted so you win!\n");
    }
    else if (cnt>=5) {
      write_user(user,"~FT~OLThe dealer says:~RS I've got a five card hand, so you lose!\n");
    }
    else if (dealer_total>user_total) {
      write_user(user,"~FT~OLThe dealer says:~RS I've beaten yer score so you lose.\n");
    }
    else if (dealer_total<user_total) {
      write_user(user,"~FT~OLThe dealer says:~RS You've beaten me!\n");
    }
    else {
      write_user(user,"~FT~OLThe dealer says:~RS Push! We've both got the same score so it's a draw.\n");
    }
#endif
    destruct_blackjack_game(user);
    return;
  }
#if USE_MONEY_SYSTEM
  write_user(user,"Usage: bjack deal [<ante>]/hit/stand/double/surrender/status\n");
#else
  write_user(user,"Usage: bjack deal/hit/stand/surrender/status\n");
#endif
}


/* display to the user their hand of cards for blackjack */
void show_blackjack_cards(UR_OBJECT user,int dealer,int start)
{
  int h,d,hand[5];
  char buff[80];
  
  if (dealer && start) {
    for (d=0;d<5;d++) {
      buff[0]='\0';
      for (h=0;h<5;++h) {
	if (user->bj_game->dealer_hand[h]==-1) continue;
	if (h==0) strcat(buff,cards[52][d]);
	else strcat(buff,cards[user->bj_game->dealer_hand[h]][d]);
      }
      sprintf(text,"%s\n",buff);
      write_user(user,text);
    }
    return;
  }
  if (!dealer) for (h=0;h<5;h++) hand[h]=user->bj_game->hand[h];
  else for (h=0;h<5;h++) hand[h]=user->bj_game->dealer_hand[h];
  for (d=0;d<5;d++) {
    buff[0]='\0';
    for (h=0;h<5;++h) {
      if (hand[h]==-1) continue;
      strcat(buff,cards[hand[h]][d]);
    }
    sprintf(text,"%s\n",buff);
    write_user(user,text);
  }
}


/* Get the total of the users or dealers hand of cards */
int check_blackjack_total(UR_OBJECT user,int dealer)
{
  int h,total,i,has_ace,all_aces_one;
  
  has_ace=all_aces_one=0;
  while (1) {
    total=0;
    /* get the user's total */
    if (!dealer) {
      for (h=0;h<5;h++) {
	if (user->bj_game->hand[h]==-1) continue;
	i=user->bj_game->hand[h]%13;
	switch(i) {
        case 0:
	  if (!has_ace) {
	    has_ace=1;  total+=11;
	  }
	  else total++;
	  break;
        case 10: /* Jacks, Queens and Kings */
        case 11:
        case 12: total+=10; break;
        default: total+=(i+1);  break;
        }
      }
    }
    /* get dealer's total */
    else {
      for (h=0;h<5;h++) {
	if (user->bj_game->dealer_hand[h]==-1) continue;
	i=user->bj_game->dealer_hand[h]%13;
	switch(i) {
        case 0:
	  if (!has_ace) {
	    has_ace=1;  total+=11;
	  }
	  else total++;
	  break;
        case 10: /* Jacks, Queens and Kings */
        case 11:
        case 12: total+=10; break;
        default: total+=(i+1);  break;
        }
      }
    }
    if (total>21 && has_ace && !all_aces_one) all_aces_one=1;
    else return total;
  }
}

