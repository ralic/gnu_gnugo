/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This is GNU GO, a Go program. Contact gnugo@gnu.org, or see       *
 * http://www.gnu.org/software/gnugo/ for more information.          *
 *                                                                   *
 * Copyright 1999, 2000, 2001, 2002 by the Free Software Foundation. *
 *                                                                   *
 * This program is free software; you can redistribute it and/or     *
 * modify it under the terms of the GNU General Public License as    *
 * published by the Free Software Foundation - version 2             *
 *                                                                   *
 * This program is distributed in the hope that it will be useful,   *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of    *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the     *
 * GNU General Public License in file COPYING for more details.      *
 *                                                                   *
 * You should have received a copy of the GNU General Public         *
 * License along with this program; if not, write to the Free        *
 * Software Foundation, Inc., 59 Temple Place - Suite 330,           *
 * Boston, MA 02111, USA.                                            *
\* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "gnugo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "liberty.h"
#include "interface.h"
#include "sgftree.h"

#define DEBUG_COMMANDS "\
capture <pos>    try to capture indicated group\n\
dead             Toggle display of dead stones\n\
defend <pos>     try to defend indicated group\n\
listdragons      print dragon info \n\
showarea         display area\n\
showdragons      display dragons\n\
showmoyo         display moyo\n\
showterri        display territory\n\
"

/* some options for the ascii interface */
static int opt_showboard = 1;
static int showdead = 0;
static int emacs = 0;
SGFTree sgftree;
SGFNode *curnode = 0;
static int last_move_i;      /* The position of the last move */
static int last_move_j;      /* -""-                          */

/* Unreasonable score used to detect missing information. */
#define NO_SCORE 4711
/* Keep track of the score estimated before the last computer move. */
static int current_score_estimate = NO_SCORE;

static void endgame(Gameinfo *gameinfo);
static void showcapture(Position *pos, char *line);
static void showdefense(Position *pos, char *line);
static void ascii_goto(Gameinfo *gameinfo, char *line);
static int ascii2pos(Position *pos, char *line, int *i, int *j);

/* If sgf game info is written can't reset parameters like handicap, etc. */
static int sgf_initialized = 0;

/*
 * Create letterbar for the top and bottom of the ASCII board.
 */

static void
make_letterbar(int boardsize, char *letterbar)
{
  int i, letteroffset;
  char spaces[64];
  char letter[64];

  if (boardsize <= 25)
    strcpy(spaces, " ");
  strcpy(letterbar, "   ");
  
  for (i = 0; i < boardsize; i++) {
    letteroffset = 'A';
    if (i+letteroffset >= 'I')
      letteroffset++;
    strcat(letterbar, spaces);
    sprintf(letter, "%c", i+letteroffset);
    strcat(letterbar, letter);
  }
}


/* This array contains +'s and -'s for the empty board positions.
 * hspot_size contains the board size that the grid has been
 * initialized to.
 */

static int hspot_size;
static char hspots[MAX_BOARD][MAX_BOARD];


/*
 * Mark the handicap spots on the board.
 */

static void
set_handicap_spots(int boardsize)
{
  if (hspot_size == boardsize)
    return;
  
  hspot_size = boardsize;
  
  memset(hspots, '.', sizeof(hspots));

  /* small sizes are easier to hardwire... */
  if (boardsize == 2 || boardsize == 4)
    return;
  if (boardsize == 3) {
    /* just the middle one */
    hspots[boardsize/2][boardsize/2] = '+';
    return;
  }
  if (boardsize == 5) {
    /* place the outer 4 */
    hspots[1][1] = '+';
    hspots[boardsize-2][1] = '+';
    hspots[1][boardsize-2] = '+';
    hspots[boardsize-2][boardsize-2] = '+';
    /* and the middle one */
    hspots[boardsize/2][boardsize/2] = '+';
    return;
  }

  if (!(boardsize%2)) {
    /* If the board size is even, no center handicap spots. */
    if (boardsize > 2 && boardsize < 12) {
      /* Place the outer 4 only. */
      hspots[2][2] = '+';
      hspots[boardsize-3][2] = '+';
      hspots[2][boardsize-3] = '+';
      hspots[boardsize-3][boardsize-3] = '+';
    }
    else {
      /* Place the outer 4 only. */
      hspots[3][3] = '+';
      hspots[boardsize-4][3] = '+';
      hspots[3][boardsize-4] = '+';
      hspots[boardsize-4][boardsize-4] = '+';
    }
  }
  else {
    /* Uneven board size */
    if (boardsize > 2 && boardsize < 12) {
      /* Place the outer 4... */
      hspots[2][2] = '+';
      hspots[boardsize-3][2] = '+';
      hspots[2][boardsize-3] = '+';
      hspots[boardsize-3][boardsize-3] = '+';

      /* ...and the middle one. */
      hspots[boardsize/2][boardsize/2] = '+';
    }
    else {
      /* Place the outer 4... */
      hspots[3][3] = '+';
      hspots[boardsize-4][3] = '+';
      hspots[3][boardsize-4] = '+';
      hspots[boardsize-4][boardsize-4] = '+';

      /* ...and the inner 4... */
      hspots[3][boardsize/2] = '+';
      hspots[boardsize/2][3] = '+';
      hspots[boardsize/2][boardsize-4] = '+';
      hspots[boardsize-4][boardsize/2] = '+';

      /* ...and the middle one. */
      hspots[boardsize/2][boardsize/2] = '+';
    }
  }

  return;
}


/*
 * Display the board position when playing in ASCII.
 */

static void
ascii_showboard(Position *pos)
{
   int i, j;
   char letterbar[64];
   int last_pos_was_move;
   int pos_is_move;
   int dead;
   
   make_letterbar(pos->boardsize, letterbar);
   set_handicap_spots(pos->boardsize);

   printf("\n");
   printf("    White has captured %d pieces\n", pos->black_captured);
   printf("    Black has captured %d pieces\n", pos->white_captured);
   if (showscore) {
     if (current_score_estimate == NO_SCORE)
       printf("    No score estimate is available yet.\n");
     else if (current_score_estimate < 0)
       printf("    Estimated score: Black is ahead by %d\n",
	      -current_score_estimate);
     else if (current_score_estimate > 0)
       printf("    Estimated score: White is ahead by %d\n",
	      current_score_estimate);
     else
       printf("    Estimated score: Even!\n");
   }
   
   printf("\n");

   fflush(stdout);
   printf("%s%s\n", (emacs ? "EMACS1\n" : ""), letterbar);
   fflush(stdout);

   for (i = 0; i < pos->boardsize; i++) {
     printf(" %2d", pos->boardsize-i);
     last_pos_was_move = 0;
     for (j = 0; j < pos->boardsize; j++) {
       if (last_move_i == i && last_move_j == j)
	 pos_is_move = 128;
       else
	 pos_is_move = 0;
       dead = (matcher_status(POS(i, j))==DEAD) && showdead;
       switch (pos->board[i][j] + pos_is_move + last_pos_was_move) {
       case EMPTY+128:
       case EMPTY:
	 printf(" %c", hspots[i][j]);
	 last_pos_was_move = 0;
	 break;
       case BLACK:
	 printf(" %c", dead ? 'x' : 'X');
	 last_pos_was_move = 0;
	 break;
       case WHITE:
	 printf(" %c", dead ? 'o' : 'O');
	 last_pos_was_move = 0;
	 break;
       case BLACK+128:
	 printf("(%c)", 'X');
	 last_pos_was_move = 256;
	 break;
       case WHITE+128:
	 printf("(%c)", 'O');
	 last_pos_was_move = 256;
	 break;
       case EMPTY+256:
	 printf("%c", hspots[i][j]);
	 last_pos_was_move = 0;
	 break;
       case BLACK+256:
	 printf("%c", dead ? 'x' : 'X');
	 last_pos_was_move = 0;
	 break;
       case WHITE+256:
	 printf("%c", dead ? 'o' : 'O');
	 last_pos_was_move = 0;
	 break;
       default: 
	 fprintf(stderr, "Illegal board value %d\n", pos->board[i][j]);
	 exit(EXIT_FAILURE);
	 break;
       }
     }

     if (last_pos_was_move == 0) {
       if (pos->boardsize > 10)
	 printf(" %2d", pos->boardsize-i);
       else
	 printf(" %1d", pos->boardsize-i);
     }
     else {
       if (pos->boardsize > 10)
	 printf("%2d", pos->boardsize-i);
       else
         printf("%1d", pos->boardsize-i);
     }
     printf("\n");
   }

   fflush(stdout);
   printf("%s\n\n", letterbar);
   fflush(stdout);
   
}  /* end ascii_showboard */

/*
 * command help
 */

static void
show_commands(void)
{
  printf("\nCommands:\n");
  printf(" back             Take back your last move\n");
  printf(" boardsize        Set boardsize (on move 1 only!)\n");
  printf(" comment          Write a comment to outputfile\n");
  printf(" depth <num>      Set depth for reading\n");
  printf(" display          Display game board\n");
  printf(" exit             Exit GNU Go\n");
  printf(" force <move>     Force a move for current color\n");
  printf(" forward          Go to next node in game tree\n");
  printf(" goto <movenum>   Go to movenum in game tree\n");
  printf(" level <amount>   Playing level (default = 10)\n");
  printf(" handicap         Set handicap (on move 1 only!)\n");
  printf(" help             Display this help menu\n");
  printf(" helpdebug        Display debug help menu\n");
  printf(" info             Display program settings\n");
  printf(" komi             Set komi (on move 1 only!)\n");
  printf(" last             Goto last node in game tree\n");
  printf(" pass             Pass on your move\n");
  printf(" play <num>       Play <num> moves\n");
  printf(" playblack        Play as Black (switch if White)\n");
  printf(" playwhite        Play as White (switch if Black)\n");
  printf(" quit             Exit GNU Go\n");
  printf(" resign           Resign the current game\n");
  printf(" save <file>      Save the current game\n");
  printf(" load <file>      Load a game from file\n");
  printf(" score            Toggle display of score On/Off\n");
  printf(" showboard        Toggle display of board On/Off\n");
  printf(" switch           Switch the color you are playing\n");
  printf(" undo             Take the last move back (same as back)\n");
  printf(" <move>           A move of the format <letter><number>");
  printf("\n");
}

enum commands {INVALID=-1, END, EXIT, QUIT, RESIGN, 
	       PASS, MOVE, FORCE, SWITCH,
	       PLAY, PLAYBLACK, PLAYWHITE,
	       SETHANDICAP, SETBOARDSIZE, SETKOMI,
	       SETDEPTH,
               INFO, DISPLAY, SHOWBOARD, HELP, UNDO, COMMENT, SCORE,
               CMD_DEAD, CMD_BACK, CMD_FORWARD, CMD_LAST,
               CMD_CAPTURE, CMD_DEFEND,
               CMD_HELPDEBUG, CMD_SHOWAREA, CMD_SHOWMOYO, CMD_SHOWTERRI,
               CMD_GOTO, CMD_SAVE, CMD_LOAD, CMD_SHOWDRAGONS, CMD_LISTDRAGONS,
	       SETHURRY, SETLEVEL, NEW, COUNT
};


/*
 * Check if we have a valid command.
 */

static int
get_command(char *command)
{
  char c;
  int d;

  /* Check to see if a move was input. */
  if (!((sscanf(command, "%c%d", &c, &d) != 2)
	|| ((c = toupper((int) c)) < 'A')
	|| ((c = toupper((int) c)) > 'Z')
	|| (c == 'I')))
    return MOVE;

  /* help shortcut */
  if (command[0] == '?')
    return HELP;

  /* Kill leading spaces. */
  while (command[0] == ' ')
    command++;

  if (!strncmp(command, "playblack", 9)) return PLAYBLACK;
  if (!strncmp(command, "playwhite", 9)) return PLAYWHITE;
  if (!strncmp(command, "showboard", 9)) return SHOWBOARD;
  if (!strncmp(command, "showdragons", 9)) return CMD_SHOWDRAGONS;
  if (!strncmp(command, "listdragons", 9)) return CMD_LISTDRAGONS;
  if (!strncmp(command, "boardsize", 9)) return SETBOARDSIZE;
  if (!strncmp(command, "handicap", 8)) return SETHANDICAP;
  if (!strncmp(command, "display", 7)) return DISPLAY;
  if (!strncmp(command, "helpdebug", 7)) return CMD_HELPDEBUG;
  if (!strncmp(command, "resign", 6)) return RESIGN;
  if (!strncmp(command, "showmoyo", 6)) return CMD_SHOWMOYO;
  if (!strncmp(command, "showterri", 6)) return CMD_SHOWTERRI;
  if (!strncmp(command, "showarea", 6)) return CMD_SHOWAREA;
  if (!strncmp(command, "depth", 5)) return SETDEPTH;
  if (!strncmp(command, "switch", 5)) return SWITCH;
  if (!strncmp(command, "komi", 4)) return SETKOMI;
  if (!strncmp(command, "play", 4)) return PLAY;
  if (!strncmp(command, "info", 4)) return INFO;
  if (!strncmp(command, "force", 4)) return FORCE;
  if (!strncmp(command, "hurry", 5)) return SETHURRY;
  if (!strncmp(command, "level", 5)) return SETLEVEL;
  if (!strncmp(command, "pass", 4)) return PASS;
  if (!strncmp(command, "save", 3)) return CMD_SAVE;
  if (!strncmp(command, "load", 3)) return CMD_LOAD;
  if (!strncmp(command, "end", 3)) return END;
  if (!strncmp(command, "move", 3)) return MOVE;
  if (!strncmp(command, "undo", 3)) return UNDO;
  if (!strncmp(command, "comment", 3)) return COMMENT;
  if (!strncmp(command, "score", 3)) return SCORE;
  if (!strncmp(command, "dead", 3)) return CMD_DEAD;
  if (!strncmp(command, "capture", 3)) return CMD_CAPTURE;
  if (!strncmp(command, "defend", 3)) return CMD_DEFEND;
  if (!strncmp(command, "exit", 4)) return EXIT;
  if (!strncmp(command, "quit", 4)) return QUIT;
  if (!strncmp(command, "help", 1)) return HELP;
  if (!strncmp(command, "back", 2)) return CMD_BACK;
  if (!strncmp(command, "forward", 2)) return CMD_FORWARD;
  if (!strncmp(command, "last", 2)) return CMD_LAST;
  if (!strncmp(command, "goto", 2)) return CMD_GOTO;
  if (!strncmp(command, "game", 2)) return NEW;
  if (!strncmp(command, "count", 2)) return COUNT;

  /* No valid command was found. */
  return INVALID;
}


/*
 * Write sgf root node. 
 */

static void
init_sgf(Gameinfo *ginfo, SGFNode *root)
{
  if (sgf_initialized)
    return;
  sgf_initialized = 1;

  sgffile_write_gameinfo(ginfo, "ascii");
  if (ginfo->handicap > 0)
    gnugo_recordboard(&ginfo->position, root);
  else
    ginfo->to_move = BLACK;
}


/*
 * Generate the computer move. 
 */

static void
computer_move(Gameinfo *gameinfo, int *passes)
{
  int i, j;
  int move_val;

  init_sgf(gameinfo, sgftree.root);
  
  /* Generate computer move. */
  move_val = gnugo_genmove(&gameinfo->position, &i, &j, gameinfo->to_move, 
			   gameinfo->move_number);
  if (showscore) {
    gnugo_estimate_score(&gameinfo->position, &lower_bound, &upper_bound);
    current_score_estimate = (int) ((lower_bound + upper_bound) / 2.0);
  }
    
  last_move_i = i;
  last_move_j = j;
  
  mprintf("%s(%d): %m\n", color_to_string(gameinfo->to_move),
	  gameinfo->move_number+1, i, j);
  if (is_pass(POS(i, j)))
    (*passes)++;
  else
    *passes = 0;

  gnugo_play_move(&gameinfo->position, i, j, gameinfo->to_move);
  curnode = sgfAddPlay(curnode, gameinfo->to_move, i, j);

  sgffile_move_made(i, j, gameinfo->to_move, move_val);
  
  gameinfo->move_number++;
  gameinfo->to_move = gameinfo->to_move == BLACK ? WHITE : BLACK;
}


/*
 * Make a move.
 */

static void
do_move(Gameinfo *gameinfo, char *command, int *passes, int force)
{
  int i, j;

  if (!ascii2pos(&gameinfo->position, command, &i, &j)) {
    printf("\nInvalid move: %s\n", command);
    return;
  }
  
  if (!gnugo_is_legal(&gameinfo->position, i, j, gameinfo->to_move)) {
    printf("\nIllegal move: %s", command);
    return;
  }

  *passes = 0;
  gameinfo->move_number++;
  TRACE("\nyour move: %m\n\n", i, j);
  gnugo_play_move(&gameinfo->position, i, j, gameinfo->to_move);
  /* FIXME: This call to init_sgf should not be here. */
  init_sgf(gameinfo, sgftree.root);
  sgffile_move_made(i, j, gameinfo->to_move, 0);
  curnode = sgfAddPlay(curnode, gameinfo->to_move, i, j);

  last_move_i = i;
  last_move_j = j;
  
  if (opt_showboard && !emacs) {
    ascii_showboard(&gameinfo->position);
    printf("GNU Go is thinking...\n");
  }
  if (force) {
    gameinfo->computer_player = OTHER_COLOR(gameinfo->computer_player);
    gameinfo->to_move = OTHER_COLOR(gameinfo->to_move);
    return;
  }
  gameinfo->to_move = OTHER_COLOR(gameinfo->to_move);
  computer_move(gameinfo, passes);
}


/*
 * Make a pass.
 */

static void
do_pass(Gameinfo *gameinfo, int *passes, int force)
{
  (*passes)++;
  gnugo_play_move(&gameinfo->position, -1, -1, gameinfo->to_move);
  gameinfo->move_number++;
  init_sgf(gameinfo, sgftree.root);
  sgffile_move_made(-1, -1, gameinfo->to_move, 0);
  curnode = sgfAddPlay(curnode, gameinfo->to_move, -1, -1);

  gameinfo->to_move = OTHER_COLOR(gameinfo->to_move);
  if (force) {
    gameinfo->computer_player = OTHER_COLOR(gameinfo->computer_player);
    return;
  }
  computer_move(gameinfo, passes);
}


/*
 * Play a game against an ascii client.
 */

void
play_ascii(SGFTree *tree, Gameinfo *gameinfo, char *filename, char *until)
{
  int m, num;
  int sz = 0;
  float fnum;
  int passes = 0;  /* two passes and its over */
  int tmp;
  char line[80];
  char *line_ptr = line;
  char *command;
  char *tmpstring;
  int state = 1;
  
#ifdef HAVE_SETLINEBUF
  setlinebuf(stdout); /* Need at least line buffer gnugo-gnugo */
#else
  setbuf(stdout, NULL); /* else set it to completely UNBUFFERED */
#endif
  
  while (state == 1) {

    /* Start the move numbering at 0... */
    /* when playing with gmp it starts at 0. */
    gameinfo->move_number = 0;
    sgftree = *tree;
    
    /* No score is estimated yet. */
    current_score_estimate = NO_SCORE;
    
    if (filename) {
      gameinfo_load_sgfheader(gameinfo, sgftree.root);
      sgffile_write_gameinfo(gameinfo, "ascii");
      gameinfo->to_move = gameinfo_play_sgftree(gameinfo, sgftree.root, until);
      sgfOverwritePropertyInt(sgftree.root, "HA", gameinfo->handicap);
      sgf_initialized = 1;
      curnode = sgftreeNodeCheck(&sgftree, 0);
    }
    else {
      if (sz)
	sgfOverwritePropertyInt(sgftree.root, "SZ", sz);
      if (sgfGetIntProperty(sgftree.root, "SZ", &sz)) 
	gnugo_clear_position(&gameinfo->position, sz, gameinfo->position.komi);
      if (gameinfo->handicap == 0)
	gameinfo->to_move = BLACK;
      else {
	gameinfo->handicap = gnugo_placehand(&gameinfo->position, 
					     gameinfo->handicap);
	sgfOverwritePropertyInt(sgftree.root, "HA", gameinfo->handicap);
	gameinfo->to_move = WHITE;
      }
      
      curnode = sgftree.root;
    }
    
    printf("\nBeginning ASCII mode game.\n\n");
    gameinfo_print(gameinfo);
    
    /* Does the computer play first?  If so, make a move. */
    if (gameinfo->computer_player == gameinfo->to_move)
      computer_move(gameinfo, &passes);
    
    /* main ASCII Play loop */
    while (passes < 2 && !time_to_die) {
      /* Display game board. */
      if (opt_showboard)
	ascii_showboard(&gameinfo->position);
      
      /* Print the prompt */
      mprintf("%s(%d): ", color_to_string(gameinfo->to_move),
	      gameinfo->move_number+1);

      /* Read a line of input. */
      line_ptr = line;
      if (!fgets(line, 80, stdin)) {
	printf("\nThanks! for playing GNU Go.\n\n");
	return ;
      }      
      while (command = strtok(line_ptr, ";"), line_ptr = 0, command) {
	
	/* Get the command or move. */
	switch (get_command(command)) {
	case RESIGN:
	  printf("\nGNU Go wins by resignation.");
	  sgftreeWriteResult(&sgftree,
			     gameinfo->to_move == WHITE ? -1000.0 : 1000.0,
			     1);
	case END:
	case EXIT:
	case QUIT:
	  printf("\nThanks! for playing GNU Go.\n\n");
	  return ;
	  break;
	case HELP:
	  show_commands();
	  break;
	case CMD_HELPDEBUG:
	  printf(DEBUG_COMMANDS);
	  break;
	case SHOWBOARD:
	  opt_showboard = !opt_showboard;
	  break;
	case INFO:
	  printf("\n");
	  gameinfo_print(gameinfo);
	  break;
	case SETBOARDSIZE:
	  if (gameinfo->move_number > 0) {
	    printf("Boardsize can be modified on move 1 only!\n");
	    break;
	  }
	  if (sgf_initialized) {
	    printf("Boardsize cannot be changed after record is started!\n");
	    break;
	  }
	  command += 10;
	  if (sscanf(command, "%d", &num) != 1) {
	    printf("\nInvalid command syntax!\n");
	    break;
	  }
	  if (num < 2 || num > 25) {
	    printf("\nInvalid board size: %d\n", num);
	    break;
	  }
	  sz = num;
	  /* Init board. */
	  gnugo_clear_position(&gameinfo->position, sz, 
			       gameinfo->position.komi);
	  /* In case max handicap changes on smaller board. */
	  gameinfo->handicap = gnugo_placehand(&gameinfo->position, 
					       gameinfo->handicap);
	  sgfOverwritePropertyInt(sgftree.root, "SZ", sz);
	  sgfOverwritePropertyInt(sgftree.root, "HA", gameinfo->handicap);
	  break;
	case SETHANDICAP:
	  if (gameinfo->move_number > 0) {
	    printf("Handicap can be modified on move 1 only!\n");
	    break;
	  }
	  if (sgf_initialized) {
	    printf("Handicap cannot be changed after game is started!\n");
	    break;
	  }
	  command += 9;
	  if (sscanf(command, "%d", &num) != 1) {
	    printf("\nInvalid command syntax!\n");
	    break;
	  }
	  if (num < 0 || num > MAX_HANDICAP) {
	    printf("\nInvalid handicap: %d\n", num);
	    break;
	  }
	  /* Init board. */
	  gnugo_clear_position(&gameinfo->position,
			       gameinfo->position.boardsize,
			       gameinfo->position.komi);
	  /* place stones on board but don't record sgf 
	   * in case we change more info */
	  gameinfo->handicap = gnugo_placehand(&gameinfo->position, num);
	  sgfOverwritePropertyInt(sgftree.root, "HA", gameinfo->handicap);
	  printf("\nSet handicap to %d\n", gameinfo->handicap);
	  gameinfo->to_move = WHITE;
	  break;
	case SETKOMI:
	  if (gameinfo->move_number > 0) {
	    printf("Komi can be modified on move 1 only!\n");
	    break;
	  }
	  if (sgf_initialized) {
	    printf("Komi cannot be modified after game record is started!\n");
	    break;
	  }
	  command += 5;
	  if (sscanf(command, "%f", &fnum) != 1) {
	    printf("\nInvalid command syntax!\n");
	    break;
	  }
	  gameinfo->position.komi = fnum;
	  printf("\nSet Komi to %.1f\n", fnum);
	  break;
	case SETDEPTH:
	  {
	    command += 6;
	    if (sscanf(command, "%d", &num) != 1) {
	      printf("\nInvalid command syntax!\n");
	      break;
	    }
	    mandated_depth = num;
	    printf("\nSet depth to %d\n", mandated_depth);
	    break;
	  }
	case SETLEVEL:
	  {
	    command += 6;
	    if (sscanf(command, "%d", &num) != 1) {
	      printf("\nInvalid command syntax!\n");
	      break;
	    }
	    level = num;
	    printf("\nSet level to %d\n", level);
	    break;
	  }
	  /* Level replaces hurry as of 2.7.204. This option is retained
	   * for compatibility with gnugoclient. 
	   */
	case SETHURRY:
	  {
	    command += 6;
	    if (sscanf(command, "%d", &num) != 1) {
	      printf("\nInvalid command syntax!\n");
	      break;
	    }
	    level = 10 - num;
	    printf("\nSet hurry to %d\n", 10 - level);
	    break;
	  }
	case DISPLAY:
	  if (!opt_showboard)
	    ascii_showboard(&gameinfo->position);
	  break;
	case FORCE:
	  command += 6; /* skip the force part... */
	  switch (get_command(command)) {
	  case MOVE:
	    do_move(gameinfo, command, &passes, 1);
	    break;
	  case PASS:
	    do_pass(gameinfo, &passes, 1);
	    break;
	  default:
	    printf("Illegal forced move: %s %d\n", command,
		   get_command(command));
	    break;
	  }
	  break;
	case MOVE:
	  do_move(gameinfo, command, &passes, 0);
	  break;
	case PASS:
	  do_pass(gameinfo, &passes, 0);
	  break;
	case PLAY:
	  command += 5;
	  if (sscanf(command, "%d", &num) != 1) {
	    printf("\nInvalid command syntax!\n");
	    break;
	  }
	  if (num >= 0)
	    for (m = 0; m < num; m++) {
	      gameinfo->computer_player 
		= OTHER_COLOR(gameinfo->computer_player);
	      computer_move(gameinfo, &passes);
	      if (passes >= 2)
		break;
	    }
	  else {
	    printf("\nInvalid number of moves specified: %d\n", num);
	    break;
	  }
	  break;
	case PLAYBLACK:
	  if (gameinfo->computer_player == WHITE)
	    gameinfo->computer_player = BLACK;
	  if (gameinfo->computer_player == gameinfo->to_move)
	    computer_move(gameinfo, &passes);
	  break;
	case PLAYWHITE:
	  if (gameinfo->computer_player == BLACK)
	    gameinfo->computer_player = WHITE;
	  if (gameinfo->computer_player == gameinfo->to_move)
	    computer_move(gameinfo, &passes);
	  break;
	case SWITCH:
	  gameinfo->computer_player = OTHER_COLOR(gameinfo->computer_player);
	  computer_move(gameinfo, &passes);
	  break;
	  /*FIXME: Don't forget boardupdate after back! */
	case UNDO:
	case CMD_BACK:
	  /*reloading the game*/
	  if (gameinfo->move_number > 0) {
	    gameinfo->to_move = gnugo_play_sgftree(&gameinfo->position,
						   sgftree.root,
						   &gameinfo->move_number,
						   &curnode);
	    gameinfo->move_number--;
	  }
	  else
	    printf("\nBeginning of game.\n");
	  break;
	case CMD_FORWARD:
	  if (curnode->child) {
	    gameinfo->to_move = gnugo_play_sgfnode(&gameinfo->position,
						   curnode->child,
						   gameinfo->to_move);
	    curnode = curnode->child;
	    gameinfo->move_number++;
	  }
	  else
	    printf("\nEnd of game tree.\n");
	  break;
	case CMD_LAST:
	  while (curnode->child) {
	    gameinfo->to_move = gnugo_play_sgfnode(&gameinfo->position,
						   curnode->child,
						   gameinfo->to_move);
	    curnode = curnode->child;
	    gameinfo->move_number++;
	  }
	  break;
	case COMMENT:
	  printf("\nEnter comment. Press ENTER when ready.\n");
	  fgets(line, 80, stdin);
	  sgfAddComment(curnode, line);
	  break;
	case SCORE:
	  showscore = !showscore;
	  if (!showscore)
	    current_score_estimate = NO_SCORE;
	  break;
	case CMD_DEAD:
	  showdead = !showdead;
	  break;
	case CMD_CAPTURE:
	  strtok(command, " ");
	  showcapture(&gameinfo->position, strtok(NULL, " "));
	  break;
	case CMD_DEFEND:
	  strtok(command, " ");
	  showdefense(&gameinfo->position, strtok(NULL, " "));
	  break;
	case CMD_SHOWMOYO:
	  tmp = printmoyo;
	  printmoyo = PRINTMOYO_MOYO;
	  examine_position(gameinfo->to_move, EXAMINE_DRAGONS);
	  print_moyo();
	  printmoyo = tmp;
	  break;
	case CMD_SHOWTERRI:
	  tmp = printmoyo;
	  printmoyo = PRINTMOYO_TERRITORY;
	  examine_position(gameinfo->to_move, EXAMINE_DRAGONS);
	  print_moyo();
	  printmoyo = tmp;
	  break;
	case CMD_SHOWAREA:
	  tmp = printmoyo;
	  printmoyo = PRINTMOYO_AREA;
	  examine_position(gameinfo->to_move, EXAMINE_DRAGONS);
	  print_moyo();
	  printmoyo = tmp;
	  break;
	case CMD_SHOWDRAGONS:
	  examine_position(gameinfo->to_move, EXAMINE_DRAGONS);
	  showboard(1);
	  break;
	case CMD_GOTO:
	  strtok(command, " ");
	  ascii_goto(gameinfo, strtok(NULL, " "));
	  break;
	case CMD_SAVE:
	  strtok(command, " ");
	  tmpstring = strtok(NULL, " ");
	  if (tmpstring) {
	    /* discard newline */
	    tmpstring[strlen(tmpstring)-1] = 0;
	    sgf_write_header(sgftree.root, 1, gameinfo->seed, 
			     gameinfo->position.komi, level);
	    writesgf(sgftree.root, tmpstring);
	    sgf_initialized = 0;
	    printf("You may resume the game");
	    printf(" with -l %s --mode ascii\n", tmpstring);
	    printf("or load %s\n", tmpstring);
	  }
	  else
	    printf("Please specify filename\n");
	  break;
	case CMD_LOAD:
	  strtok(command, " ");
	  tmpstring = strtok(NULL, " ");
	  if (tmpstring) {
	    /* discard newline */
	    tmpstring[strlen(tmpstring)-1] = 0;
	    if (!sgftree_readfile(&sgftree, tmpstring))
	      {
		fprintf(stderr, "Cannot open or parse '%s'\n", tmpstring);
		break;
	      }
	    sgf_initialized = 0;
	    gameinfo_play_sgftree(gameinfo, sgftree.root, NULL);
	    sgfOverwritePropertyInt(sgftree.root, "HA", gameinfo->handicap);
	  }
	  else
	    printf("Please specify a filename\n");
	  break;

	case CMD_LISTDRAGONS:
	  examine_position(gameinfo->to_move, EXAMINE_DRAGONS);
	  show_dragons();
	  break;
	case COUNT:
	case NEW:
	case INVALID:
	default:
	  printf("\nInvalid command: %s", command);
	  break;
	}
      }
    }
    
    /* two passes : game over */
    
    if (passes >= 2)
      gnugo_who_wins(&gameinfo->position, gameinfo->computer_player, stdout);
    printf("\nIf you disagree, we may count the game together.\n");
    printf("You may optionally save the game as an SGF file.\n");
    state = 0;
    while (state == 0) {
      printf("\n\
Type \"save <filename>\" to save,\n\
     \"count\" to recount,\n\
     \"quit\" to quit\n\
 or  \"game \" to play again\n");
      line_ptr = line;
      if (!fgets(line, 80, stdin))
	break;
      command = strtok(line_ptr, "");
      switch (get_command(command)) {
      case CMD_SAVE:
	strtok(command, " ");
	tmpstring = strtok(NULL, " ");
	if (tmpstring) {
	  /* discard newline */
	  tmpstring[strlen(tmpstring)-1] = 0;
	  sgf_write_header(sgftree.root, 1, gameinfo->seed, 
			   gameinfo->position.komi, level);
	  writesgf(sgftree.root, tmpstring);
	    sgf_initialized = 0;
	}
	else
	  printf("Please specify filename\n");
	break;
	
      case NEW:
	state = 1;
	break;
	
      case COUNT:
	endgame(gameinfo);
	break;

      case QUIT:
	state = 2;
	break;
	
      default:
	state = 0;
      }
    }
    passes = 0;
    showdead = 0;
    sgf_initialized = 0;
  }
  printf("\nThanks for playing GNU Go.\n\n");
}

void
play_ascii_emacs(SGFTree *tree, Gameinfo *gameinfo,
		 char *filename, char *until)
{
  emacs = 1;
  play_ascii(tree, gameinfo, filename, until);
}


/*
 * endgame() scores the game.
 */

static void
endgame(Gameinfo *gameinfo)
{
  char line[12];
  int done = 0;
  int i, j;
  int xyzzy = 1;
  float komi;

  komi = gameinfo->position.komi;

  printf("\nGame over. Let's count!.\n");  
  showdead = 1;
  ascii_showboard(&gameinfo->position);
  while (!done) {
    printf("Dead stones are marked with small letters (x,o).\n");
    printf("\nIf you don't agree, enter the location of a group\n\
to toggle its state or \"done\".\n");

    if (!fgets(line, 12, stdin))
      return; /* EOF or some error */
    
    for (i = 0; i < 12; i++)
      line[i] = (isupper ((int) line[i]) ? tolower ((int) line[i]) : line[i]);
    if (!strncmp(line, "done", 4))
      done = 1;
    else if (!strncmp(line, "quit", 4))
      return;
    else if (!strncmp(line, "xyzzy", 5)) {
      if (xyzzy) {
	printf("\nYou are in a debris room filled with stuff washed in from the\n");
	printf("surface.  A low wide passage with cobbles becomes plugged\n");
	printf("with mud and debris here, but an awkward canyon leads\n");
	printf("upward and west.  A note on the wall says:\n");
	printf("   Magic Word \"XYZZY\"\n\n");
	xyzzy = 0;
      }
      else {
	printf("You are inside a building, a well house for a large spring.\n\n");
	xyzzy = 1;
      }
    }
    else if (!strncmp(line, "help", 4)) {
      printf("\nIf there are dead stones on the board I will remove them.\n");
      printf("You must tell me where they are. Type the coordinates of a\n");
      printf("dead group, or type \"done\"\n");
    }      
    else if (!strncmp(line, "undo", 4)) {
      printf("UNDO not allowed anymore. The status of the stones now toggles after entering the location of it.\n");
      ascii_showboard(&gameinfo->position);
    }
    else {
      if (!ascii2pos(&gameinfo->position, line, &i, &j)
	  || gameinfo->position.board[i][j] == EMPTY)
	printf("\ninvalid!\n");
      else {
	int status = matcher_status(POS(i, j));
	status = (status == DEAD) ? ALIVE : DEAD;
	change_matcher_status(POS(i, j), status);
	ascii_showboard(&gameinfo->position);
      }
    }
  }
  gnugo_who_wins(&gameinfo->position, gameinfo->computer_player, stdout);
}


static void
showcapture(Position *pos, char *line)
{
  int i, j, x, y;
  if (line)
    if (!ascii2pos(pos, line, &i, &j) || pos->board[i][j] == EMPTY) {
      printf("\ninvalid point!\n");
      return;
    }
  if (gnugo_attack(pos, i, j, &x, &y))
    mprintf("\nSuccessfull attack of %m at %m\n", i, j, x, y);
  else
    mprintf("\n%m cannot be attacked\n", i, j);
}


static void
showdefense(Position *pos, char *line)
{
  int i, j, x, y;
  if (line)
    if (!ascii2pos(pos, line, &i, &j) || pos->board[i][j] == EMPTY) {
      printf("\ninvalid point!\n");
      return;
    }

    if (gnugo_attack(pos, i, j, &x, &y)) {
      if (gnugo_find_defense(pos, i, j, &x, &y))
        mprintf("\nSuccessfull defense of %m at %m\n", i, j, x, y);
      else
        mprintf("\n%m cannot be defended\n", i, j);
    }
    else
      mprintf("\nThere is no need to defend %m\n", i, j);
}


static void
ascii_goto(Gameinfo *gameinfo, char *line)
{
  int movenumber = 0;
  if (!line)
    return;
  if (!strncmp(line, "last", 4))
    movenumber = 9999;
  else {
    if (!strncmp(line, "first", 4))
      movenumber = 1;
    else
      sscanf(line, "%i", &movenumber);
  }
  printf("goto %i\n", movenumber);
  curnode = sgftree.root;
  gameinfo->to_move = gnugo_play_sgftree(&gameinfo->position, curnode,
					 &movenumber, &curnode);
  gameinfo->move_number = movenumber-1;
  return;
}


/* Convert a coordinate pair from ascii text to two integers.
 * FIXME: Check that this function is indeed equivalent to
 * string_to_location() and then replace it.
 */
static int
ascii2pos(Position *pos, char *line, int *i, int *j)
{
  int d;
  char c;
  if (sscanf(line, "%c%d", &c, &d) != 2)
    return 0;

  /* No 'I' in the coordinate system. */
  if (tolower((int) c) == 'i')
    return 0;
  
  *j = tolower((int) c) - 'a';
  if (tolower((int) c) > 'i')
    --*j;
  
  *i = pos->boardsize - d;

  if (*i < 0 || *i >= pos->boardsize || *j < 0 || *j >= pos->boardsize)
    return 0;
  
  return 1;
}


/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
