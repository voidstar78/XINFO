/*
voidstar - 2023

*/
#include <string.h>        //< memcpy, memset, strlen, strstr, strchr, strcpy
#include <stdlib.h>        //< Used for itoa, srand and exit (mostly for exit in this one)
#include <stdio.h>         //< printf, sscanf and fopen, fgetc
#include <conio.h>         //< cgetc, gotox/y, cbm_open, cbm_close

/*
NOTE: printf is not needed by this code.  Only reason printf is used is as a
convenient way to show "parsing errors" during input of the .nfo document files.
i.e. to show the row/column of any parsing issues (like incomplete tokens, etc).
If we assume those inputs are "perfect" then no error checking is needed.
However, stdio.h is still needed for file-io stuffs.  But if we remove the printf
calls, that drops the code size at least 2KB or so.

I do use sscanf once, but only for a hex conversion, should be able to easily replace
that with a standalone function.  sscanf otherwise is another big waste of code space.
*/

#define TRUE  1
#define FALSE 0

#define POKE(addr,val)     (*(char*) (addr) = (val))
#define POKEW(addr,val)    (*(unsigned*) (addr) = (val))
#define PEEK(addr)         (*(char*) (addr))
#define PEEKW(addr)        (*(unsigned*) (addr))

#define CLRSCR \
  __asm__("lda #$93"); \
  __asm__("jsr $FFD2");
	
// Enabling ISO mode also clears the screen
#define ENABLE_ISO_MODE \
	__asm__("lda #$0F"); \
	__asm__("jsr $FFD2");

// "ending" ISO mode = returning to normal PETSCII mode
#define DISABLE_ISO_MODE \
	__asm__("lda #$8F"); \
	__asm__("jsr $FFD2");

// NOTE: GETIN == $FFE4
char ch_result;
#define GETCH_WAIT \
  __asm__("WAITL%s:", __LINE__); \
  __asm__("jsr GETIN"); \
  __asm__("cmp #0");   \
  __asm__("beq WAITL%s", __LINE__);  \
  __asm__("sta %v", ch_result);
// the "cmp" above may not be necessary, but didn't verify that yet.
// Not using the "blocking" GETCH anyway.

#define GETCH \
  __asm__("jsr GETIN"); \
  __asm__("sta %v", ch_result);

#define ERR_NO_ARG -1
#define ERR_FILE_NOT_FOUND -2
#define ERR_VISIBLE_WIDTH -3
#define ERR_ESCAPE -4
#define ERR_FILE_ISSUE -5
#define ERR_MARGIN_RIGHT -6

#define KEY_ESC 27
#define KEY_F1 133
#define KEY_BACKSPACE 20
#define KEY_ENTER 13
#define KEY_TAB 9
#define KEY_SHIFT_TAB 24
#define KEY_UP 145
#define KEY_DOWN 17
#define KEY_BRACKET_LEFT 91
#define KEY_BRACKET_RIGHT 93
// ----------------------------------- special commands
#define CMD_WRAP_OFF 0xFF
#define CMD_LINE_SEPERATOR 0xFE  // aka DIVIDER
#define CMD_MENU 0xFD  // aka PAUSE
// !!! remember to adjust is_visible if you add more special commands

#define MAX_LINKS_PER_PAGE 30
/*
"link codes" are non-printable characters that are detected during
physical content output, to help determine the x/y coordinate of where
that output has "landed" relative to the current screen size.  These
are embeded markers checked for during physical output, and the array
position offset is 1:1 related to the index into the link_data array
defined later.   i.e. the first displayed link is associated with the
first link_code, and so on.

Normally the first 32 codes of the lower 128 and upper 128 characters
are non-printable, but PETSCII has some color codes in there.  We
could probably use some other codes, like $FF.  Anyway, since the codes
might not be all in a contiguous region, the following array just helps
identify them.
*/
char link_code[] = {
	// lower 32
	0x02,0x03,  //2
	0x04,0x06,  //4
	0x07,0x0A,  //6
	0x0B,0x0C,  //8
	0x10,0x13,  //10
	0x14,0x15,  //12
	0x16,0x17,  //14
	0x18,0x19,  //16
	0x1A,0x1B,  //18
	// -------------------
	// upper 32
	0x82,0x83,  //20
	0x84,0x85,  //22
	0x86,0x87,  //24
	0x88,0x89,  //26
	0x8A,0x8B,  //28
	0x8C,0x8D   //30
};

/*

cc65 supports passing of command line arguments like this:

RUN:REMarg1 arg2 arg3 arg4

From my own experimental testing with the version of cc65 I had:

- It doesn't matter if there are spaces between REM and arg1 or not.  arg1 will ont include
  those spaces nor include the REM
- The parsing DOES support quoted arguments, so that argument can contain spaces.
  e.g.  arg1 "  arg2    " arg3, where arg2 will = "  arg2    " (arg2 flanked by spaces)
- argv[0] will not be the name of the program, but appears to instead
  always be a string representing the number of arguments that was passed.
- argc seems to be initialized properly to also be that number of arguments.

Future ideas of passing arguments:
- assume some fixed memory region?
- use some BASIC variables? like FI$ for filename?

*/

typedef struct
{
  char header[6];
  char margin_top;
  char margin_bottom;
  char margin_left;
  char margin_right;
  char n_fn;					// file number to be used by cbm_open
  char n_device;			// device number to be used by cbm_open
  char n_sec_addr;		// channel or secondary address to be used by cbm_open
  
  // Used for "parsing help" for when users make some syntax errors in specifying mark-up tokens.
  char parse_error;
  unsigned int parsed_x;  // file col may be over 255 chars
  unsigned int parsed_y;  // file rows/length may be over 255 lines
} Program_config;
Program_config program_config_default;
Program_config* program_config_ptr;

char goto_tag_str[40];  //< populated with search string of a tag link, change 0th element to \0 when done searching
char* arg1_ptr;  //< override first "command line argument" with new filename when doing external links

char orig_video_mode;
char curr_video_mode;

char word_wrap_mode;
char file_still_open;
char current_page;

char res_x;
char res_y;
char res_x_actual;
char res_y_actual;
char res_x_actualM1;
char res_xM1;
char res_yM1;
void get_screen_resolution()
{
	__asm__("jsr $ffed");  // get screen resolution	
	__asm__("stx %v", res_x);
	__asm__("sty %v", res_y);
	
  res_x_actual = res_x;
	res_y_actual = res_y;
  res_x_actualM1 = res_x - 1;
  
	res_x -= program_config_ptr->margin_right;  // induce an artificial margin on the right side of the screen
	res_y -= program_config_ptr->margin_bottom;

	res_yM1 = res_y - 1;  // important since "last row" reserved for [menu]
	res_xM1 = res_x - 1;
}

#define SET_SCREEN_MODE \
	__asm__("clc"); \
	__asm__("sta %v", curr_video_mode); \
	__asm__("jsr $ff5f");

char temp_x;
char temp_y;
void reset_mouse()
{	
  // turn off, calling mouse_config
	__asm__("lda #$0");	
	__asm__("ldx #$0");	
	__asm__("ldx #$0");	
	__asm__("jsr $FF68");
	// turn back on
	__asm__("lda #$1");
  __asm__("ldx %v", res_x_actual);
	__asm__("ldy %v", res_y_actual);	
	__asm__("jsr $FF68");  // call mouse_config
}

char cursor_x;  // COLUMN (0-N)
char cursor_y;  // ROW (0-N)
void get_cursor_xy()
{
	__asm__("sec");
	__asm__("jsr $fff0");
	__asm__("stx %v", cursor_y);  // store the ROW
	__asm__("sty %v", cursor_x);  // store the COLUMN
	
	// The following is necessary only to address an issue with the DIVIDER line (0xFE)
	// that is not followed by a newline.
	while (cursor_x >= res_x)
	{
		cursor_x -= res_x;
		cursor_y += 1;
	}
}

unsigned int mouse_x = 0;  // column (0-N)
unsigned int mouse_y = 0;  // row (0-N)
char mouse_buttons = 0;
void get_mouse_textmode()  // "textmode" just cause the final x/y are divided by 8
{
	__asm__("ldx #$70");  // where in zeropage to store mouse x/y
	__asm__("jsr $FF6B");  // mouse_get
	__asm__("sta %v", mouse_buttons);
	// NOTE: Reg.X contains mouse wheel info (not currently used by XINFO)

	mouse_x = (((unsigned int)PEEK(0x0071) << 8) | (unsigned int)PEEK(0x0070)) >> 3;  // >>3 for /8, textmode 
	mouse_y = (((unsigned int)PEEK(0x0073) << 8) | (unsigned int)PEEK(0x0072)) >> 3;  // >>3 for /8, textmode 
  // NOTE: the above has a 16-bit intermediate (but doesn't really go beyond 640 or 10 bits)
}

// Input buffer for reading from current file being viewed.  Should have enough room
// for a couple text mode rows (80x2 worse case) plus provision for non-printable color codes.
// Eats up some RAM, so keep it "not too large."
#define BUFFER_LEN 250U
char buffer[BUFFER_LEN];
char buffer_idx = 0;

// Token commands during input, like "<CONTROL:"
char cmd_stack[20];
char cmd_stack_idx = 0;

// Value associated with the token; could be a full link target path plus description, so could get long
char value_stack[120];
char value_stack_idx = 0;

char g_i;		// general purpose loop counter
char g_i2;		// general purpose loop counter
char g_i3;		// general purpose loop counter

char g_f;							// current file result (cbm_open result)
char g_eof;						// global "end of file" indicator

// LT = link type
#define LT_EXTERNAL 0
#define LT_TAG 1
typedef struct
{
	char link_len;  // length of the "link description" text (not the length of the link_ref)
	// note: we don't have to store the actual link description, since "what we're clicking on" isn't important,
	// we just need to know "where" to click

	char link_ref[60];  // link_ref can be a full path like "/manuals/good-stuff/longfilename.nfo"
	char link_type;  // 0 = external, 1 = tag
	char cursor_x;  // COLUMN
	char cursor_y;  // ROW
} Link_data;
Link_data link_data[MAX_LINKS_PER_PAGE];
char link_data_idx = 0;
char curr_link_tab_idx;
char curr_link_mouse_idx;  // will be 0xFF is mouse is not over any link
/*
link_data_idx represents how many links are currently being displayed.
(i.e. NOT the total number of links within the file)
*/

/*
NOTE: The above pre-allocated array start to eat up RAM quickly.
We could consider putting them in different BANKS, just to learn how that's done from cc65?
*/

char is_visible(char ch)  // i.e. is this character code visible when printed to the console? 
{
	char result = FALSE;  // assume no...
	
	if (
		((ch >= 0x20) && (ch <= 0x7E))  
		|| ((ch >= 0xA1) && (ch < 0xFD))  // FF = word wrap set, FE = divider line, FD = forced menu
		// 0x7F considered as not visible in ISO
		// 0xA0 considered as not visible in ISO
	)
	{
		result = TRUE;
	}
	
	return result;
}

char g_ch = '\0';
char visible_width;  // accounting for how much of the current text mode row has been used
char visible_height; // accounting for how many rows into the current text mode we've drawn on
// If the thing we're printing (g_ch) is intended to physically displayed, pass 1 to indicate
// it is visible and (virtually) account for that text mode width being consumed.  Otherwise,
// if it's a non-printable character (color change, etc) pass 0.
#define IS_VISIBLE 1
#define NOT_VISIBLE 0
void VIRTUAL_OUT(char visible)
{
	// handle special case of the very first output, to apply the top and left margin
	if (	  
		(visible_width == program_config_ptr->margin_left)  // we're on the left margin column
		&& (visible_height == 0)  // we've cleared the screen or are somehow at the first row
		&& (program_config_ptr->margin_top > 0)  // and some top margin has been specified...
	)
	{
		// this will induce visible_height to match the margin_top
		for (g_i2 = 0; g_i2 < program_config_ptr->margin_top; ++g_i2)
		{
			__asm__("lda #$0D");  // enter
			__asm__("jsr $ffd2");
			++visible_height;
		}		
		
		for (g_i2 = 0; g_i2 < program_config_ptr->margin_left; ++g_i2)
		{
			__asm__("lda #$1D");  // cursor right, avoid overwriting by not using $20 SPACE
			__asm__("jsr $ffd2");			
		}
		
		// need to "re-learn" was the prior visible width (sub-portion may have caused a word wrap)
		for (g_i2 = 0; g_i2 < buffer_idx; ++g_i2)
		{
			if (is_visible(buffer[g_i2]))
			{
				++visible_width;
			}
		}		
	}
	
	buffer[buffer_idx] = g_ch;
	++buffer_idx;
	visible_width += visible;
}

#define PRINT_OUT_RAW(d) \
  __asm__("lda #%s", d); \
  __asm__("jsr $FFD2"); 

#define PRINT_OUT_RAW_CHAR \
  __asm__("lda %v", g_ch); \
  __asm__("jsr $FFD2"); 

/*
There can be situation where SPACE is inducing a word wrap, or the wrap has occured
in a screen resolution in such a way that a SPACE ends up at the beginning of the next
line.  This is especially in situations where period is "more correctly" followed by
two spaces, and the logic is that the SPACE that causes the wrap is replaced by a
newline (and yet the subsequent single SPACE still remains).  Bottom line is, the
"skip_spaces_mode" is to address these situations and avoid a SPACE being placed at
the beginning of a line.
*/
char skip_spaces_mode = TRUE;

void PRINT_OUT()
{
	char is_new_line = FALSE;  // assume not a new line
	
	if (skip_spaces_mode && (g_ch == ' '))
	{
		return;
	}
	else
	{
		skip_spaces_mode = FALSE;
	}
  
  if (g_ch == CMD_WRAP_OFF)
  {
    word_wrap_mode = FALSE;    
    return;
  }
	
	if (goto_tag_str[0] != '\0')
	{
		return;  // don't actually physically print, we're still in "search for tag" mode
	}

	if (g_ch == KEY_ENTER)  // ENTER/RETURN
	{
		++visible_height;  // account for how many text-mode rows have been used up (to know when to show the MENU)
		is_new_line = TRUE;

    if (word_wrap_mode == FALSE)
    {
			get_cursor_xy();
      if (cursor_x == res_xM1)  // happens when no right side margin specified (i.e. it is 0)
      {
        // we might be here because word-wrap is off, we reached edge, and printed > and moved cursor back left.
        // however we ended up here, if we print a new line it might end up as a double printed line
        PRINT_OUT_RAW('>');  //< via CMDR-DOS, cursor moves to next line when writing at physical edge
        word_wrap_mode = TRUE;
        goto auto_output;
      }
    }
 
		word_wrap_mode = TRUE;  // force word wrap mode to be back ON at end of line    
		goto auto_output;
	}
	
	get_cursor_xy();
	if (
		(cursor_x >= res_xM1)  // hit "edge" of the screen [res_x already includes right margin]
		&& (word_wrap_mode == FALSE)  // if true, then don't look for a word wrap cut off (since we're not in word wrapping mode!)
	)
	{
		if (is_visible(g_ch) == TRUE)   // no word wrap, just let visible stuff "go off the edge of the screen"
		{
			return;
		}
		// else- non-printable characters do color change effects, so we want to be sure to capture the last one of
		// those so when the text continues it is the expected color
		goto auto_output;
	}	

	// of all the current links buffered up, search them to see if g_ch is a nonprintable code
	// that corresponds to where we will output the link text.
	for (g_i2 = 0; g_i2 < link_data_idx; ++g_i2)
	{
		// Understand that this is pretty slow, since it happens for EACH printed output.  
		// Yep, totally taking the 8MHz for granted here.  At 80x60 full wall of text, this 
		// starts to get more apparent. Probably some smarter way to handle this.
		// But, there probably aren't too links on the current page.
		if (g_ch == link_code[g_i2])
		{
			get_cursor_xy();
			// store current cursor x/y, to help with link-clicking later
			link_data[g_i2].cursor_x = cursor_x;
			link_data[g_i2].cursor_y = cursor_y;
			
			//printf("<%d %d %d>", g_i2, cursor_x, cursor_y);
			break;
		}
	}
	if (g_i2 == link_data_idx)  // special non-printable link code not found
	{
		// go ahead and "physically print" the character being asked to be displayed
auto_output:
		__asm__("lda %v", g_ch);
		__asm__("jsr $FFD2");
	}
	if (
		(is_new_line == TRUE)
	)
	{
		for (g_i2 = 0; g_i2 < program_config_ptr->margin_left; ++g_i2)
		{
			__asm__("lda #$1D");  // cursor right, instead of $20 space
			__asm__("jsr $ffd2");
		}
		skip_spaces_mode = TRUE;		
    // WARNING: visible_margin_left is not set despite the above moving to the right a few spaces
	}
}

// Assess whether the current row (in whatever the current text mode resolution is) has been filled up.
// If so, go ahead and print the buffered "virtual content" of that row (including with the non-printable
// control codes to change colors).   In some cases (like we got an explicit new line early or reached
// end of file early), we need to just "force" the current buffered output.  In that case, pass "force_remaining" as TRUE,
// otherwise pass it as FALSE.
void ASSESS_OUTPUT(char force_remaining)
{
	/*
	01234567890123456789  (model 20 column width)
	AAAAAAAAAAAAAAAAAAAA  (visible_width == res_x, no wrap)
	AAAAAAAAAAAAAAAAAAA   (space at end)
	A AAAAAAAAAAAAAAAAAA
	AAA AAAAAAA AAAA AAA
	AAAAAAAAAAA AAAAAAAA
	
	AAAAAAAAAAAAA AAaAAaAA  (found a wrap w/ non-printable controls contained in the buffer)	
	              ^
	*/
	char idx_orig;
	char tmp_idx;	
	
	if (visible_width > res_x)
	{
		// We're doing like a 'pre-emptive' word wrap, not a look ahead.  This assessment
		// should be done during each individual addition to the current row.  If we buffer up beyond
		// the current row, funny stuff will happen.
    program_config_ptr->parse_error = 0x07;
		//printf("visible_width too large: %d", visible_width);
		exit(ERR_VISIBLE_WIDTH);
	}

	if (force_remaining || (visible_width >= res_xM1))  // time to output a line [note: visible_width already includes margin_left distance]
	{

		g_i = buffer_idx;  // assume we need to output the entire current buffer
		while (TRUE)
		{
      if (g_i == 0)
      {
        // the buffer is empty
        // this is a kludge; since g_i is signed char, 0 length would go to -1
        g_i = 1;
      }
			--g_i;

			if (
				(g_i == 0)  // no suitable word wrap position found
				|| (force_remaining != 0)
				|| (word_wrap_mode == FALSE)
			)
			{
				// just print the entire buffer content as-is (even if there is a space at the front)
				for (g_i = 0; g_i < buffer_idx; ++g_i)  // this is mainly for the "force_remaining" case
				{
					g_ch = buffer[g_i]; PRINT_OUT();
				}
				get_cursor_xy();
				if (cursor_x >= res_x)  // hit "edge" of the screen [res_x already includes right margin]
				{  
				  /* no longer permitting margin_right to be 0
					if (program_config_ptr->margin_right == 0)
					{
						// we get a "natural newline" by CMDR-DOS due to hitting the actual physical edge of the text mode screen
						// that is, the cursor ends up the next line anyway without an actual CR.  Account for this manually...
						++visible_height;
						// manually apply the left margin
						for (g_i2 = 0; g_i2 < program_config_ptr->margin_left; ++g_i2)
						{
							__asm__("lda #$1D");  // cursor right, instead of $20 space
							__asm__("jsr $ffd2");
						}
					}
					else
					*/
					{
						// induce a new line by hitting the right side margin
						g_ch = KEY_ENTER; PRINT_OUT();
					}
				}

				buffer_idx = 0;  // flush the buffer to start working on the next row
				visible_width = program_config_ptr->margin_left;
				break;
			}
			else if (buffer[g_i] == ' ')
			{
				// go ahead and word-wrap from the last detected space
				//printf("[2,%d.%d]", buffer_idx, g_i);
				
				tmp_idx = g_i;
				
				// output row from the beginning to that detected space position
				for (g_i = 0; g_i < tmp_idx; ++g_i)
				{
					g_ch = buffer[g_i]; PRINT_OUT();
				}

				// the space effectively now becomes a newline
				g_ch = KEY_ENTER;  PRINT_OUT();   // print_out will detect the KEY_ENTER and count a row being used
				
				// now write the remaining content that was past the space (accounting also for non-printables)
				idx_orig = tmp_idx+1;
				visible_width = program_config_ptr->margin_left;
				{
					// reconstruct a "virtual row" from the prior remaining content from where we wrapped...
					
					//printf("[%d,%d]", force_remaining, (buffer_idx-idx_orig));
					
					tmp_idx = 0;
					for (g_i = idx_orig; g_i < buffer_idx; ++g_i)
					{
						visible_width += is_visible(buffer[g_i]);
						buffer[tmp_idx] = buffer[g_i];
						++tmp_idx;
					}
					buffer_idx = tmp_idx;
				}
				
				break;
			}
		}
	}
	else
	{
		// not ready to print a line...
	}
}

char new_file = FALSE;
void check_for_link_selected()
{	
	if (link_data_idx > 0)
	{
		if (curr_link_mouse_idx != 0xFF)
		{
			g_i = curr_link_mouse_idx;
		}
		else
		{
			g_i = curr_link_tab_idx;
		}	
	
		if (link_data[g_i].link_type == LT_TAG)
		{
			// enable search for tag mode
			strcpy(goto_tag_str, link_data[g_i].link_ref);
			ch_result = ' ';  // induce pressing space to proceed from the pause/menu
		}
		else if (link_data[g_i].link_type == LT_EXTERNAL)
		{
			// override the first command line argument with this new relative or absolute path, and start all over
			strcpy(arg1_ptr, link_data[g_i].link_ref);
			new_file = TRUE;  // set flag that we're switching to a new file (note: didn't verify file exists yet)
			ch_result = ' ';  // induce pressing space to proceed from the pause/menu
		}
		// else code bug - unsupported link type				
	}
}

//static char temp_buffer[60];
void show_link_target()
{
	char max_len;
	char desc_len;	
	
	curr_link_mouse_idx = 0xFF;  // assume mouse is not over a link
	
	if (link_data_idx == 0)
	{
		// no possible links to show
		return;
	}
	
	for (g_i = 0; g_i < link_data_idx; ++g_i)
	{
		if (mouse_y == link_data[g_i].cursor_y)
		{
			if (
				(mouse_x >= link_data[g_i].cursor_x)
				&& (mouse_x <= (link_data[g_i].cursor_x + link_data[g_i].link_len))
			)
			{
				curr_link_mouse_idx = g_i;

just_show_tab_link:
				// show current link "path"
				gotoxy(8+program_config_ptr->margin_left, res_yM1);  // move past "[menu]"
				
				//get_cursor_xy();
				max_len = res_xM1 - (8+program_config_ptr->margin_left) - 2;  // - 2 is just so we don't write the link target reminder all the way to end of screen
				
				desc_len = strlen(link_data[g_i].link_ref);
				if (max_len > desc_len)
				{
					max_len = desc_len + 1;
				}
				
        PRINT_OUT_RAW('[');
        PRINT_OUT_RAW(0x9E);
				for (g_i2 = 0; g_i2 < max_len; ++g_i2)
				{
          g_i3 = link_data[g_i].link_ref[g_i2];
          __asm__("jsr $FFD2");  //< risky, assumes g_i3 will correspond to lda (which is probably the case as long as g_i3 is a char)
          //PRINT_OUT_RAW(g_i3);
					//temp_buffer[g_i2] = link_data[g_i].link_ref[g_i2];
				}
        PRINT_OUT_RAW(0x05);
        PRINT_OUT_RAW(']');
				//temp_buffer[g_i2] = '\0';
				
				// note: the temp_buffer is needed just in case the link target is really link (longer than room on the screen to fit)
				// in that case we just show the first portion of it that would fit in the screen
				
				//printf("[%c%s%c]", 0x9E, temp_buffer, 0x05);  // yellow  white
				// tbd: instead of white, we probably need to resume/revert to whatever the color was...
				
				// erase to the end of line (in case switching to showing shorter link)
				do
				{
					get_cursor_xy();
					if (cursor_x >= res_xM1)
					{
						break;
					}
          PRINT_OUT_RAW(' ');
					//printf(" ");
				} while (TRUE);
				
				break;
			}
		}
	}
	if (g_i == link_data_idx)  // no link within the mouse x/y cursor
	{
		// show the link at the current tab selection
    g_i = curr_link_tab_idx;
		goto just_show_tab_link;
	}
}

void WRITE_XX_DIGIT(char val)
{	
  char pad = 2;
	char index = 0;
	char multi = 10;	
		
	while (TRUE)
	{
		if (val < multi)
			break;
		++index;
		multi *= 10;
	}
	
	pad = pad-(index+1);
	while (pad > 0)
	{
		PRINT_OUT_RAW(0x20);  // space   
		--pad;
	}

	while (index > 0)
	{		    
		--index;
		multi /= 10;
		g_ch = 48 + (char)(val/multi);
		PRINT_OUT_RAW_CHAR;
		val %= multi;
	}
	
	g_ch = 48 + val;
	PRINT_OUT_RAW_CHAR;
}

// Return 0 for normal return 
// Return 1 for ESC pressed (exit)
// Return 2 for search for TAG mode enabled
// Return 3 for request to start over with new file
#define MENU_CONTINUE 0
#define MENU_ESC 1
#define MENU_TAG 2
#define MENU_NEW_FILE 3
#define MENU_SKIP 4
char handle_pause()  // "pause" aka "the menu" (intermission between filled up screen pages)
{	
  ++current_page;
	if (current_page > 100)
	{
		current_page = 0;
	}
	gotoxy(program_config_ptr->margin_left+6, res_yM1);
	WRITE_XX_DIGIT(current_page);
	
  gotoxy(program_config_ptr->margin_left, res_yM1);  // go to "bottom" of the current row (minus margin)
  PRINT_OUT_RAW(0x9C);  // magenta/purple
  PRINT_OUT_RAW(0x01);  // inverse
	PRINT_OUT_RAW('[');
	if (file_still_open == TRUE)
	{				
		PRINT_OUT_RAW('m');
		PRINT_OUT_RAW('o');
		PRINT_OUT_RAW('r');
		PRINT_OUT_RAW('e');		
	}
  else
	{		    
    PRINT_OUT_RAW('s');
    PRINT_OUT_RAW('t');
    PRINT_OUT_RAW('o');
    PRINT_OUT_RAW('p');    
	}
	PRINT_OUT_RAW(']');
  PRINT_OUT_RAW(0x01);  // inverse
  PRINT_OUT_RAW(0x05);  // white
	// DEBUG
	//gotoxy(program_config_ptr->margin_left, res_yM1);
	//printf("%d", buffer_idx);
 
  //printf("%c%c[menu]%c%c", 0x9C, 1, 1, 5);  // %c used to output CHAR $01 which is interpreted to reverse fg/bg (9c = purple, 5=white)
  // TODO: need a smarter way to adjust color of the menu, since reversing it makes it look like a clickable link
  // if we "force" a color, it could conflict with the current background color.
  curr_link_tab_idx = 0;  // default to choosing TAB target at the "first shown link" (valid only if there is at least one link at all)	

	mouse_buttons = 0;
	ch_result = 0;
	
	if (link_data_idx > 0)
	{
		goto init_tab_link;
	}
	
	do
	{
		GETCH;
		
		if (ch_result == KEY_BRACKET_LEFT)
		{
			if (curr_video_mode == 0)
			{
				curr_video_mode = 11;
			}
			else
			{
				--curr_video_mode;
			}
			SET_SCREEN_MODE;
			reset_mouse();
		}
		
		else if (ch_result == KEY_BRACKET_RIGHT)
		{
			if (curr_video_mode == 11)
			{
				curr_video_mode = 0;
			}
			else
			{
				++curr_video_mode;
			}
			SET_SCREEN_MODE;			
			reset_mouse();
		}		
		
		else if (link_data_idx > 0)  // if  there is at least one link...
		{
			if (ch_result == KEY_ENTER)
			{
				// go to the currently selected link
				check_for_link_selected();
			}			
			
			else if (
			  (ch_result == KEY_SHIFT_TAB)
				|| (ch_result == KEY_UP)
		  )
			{
				// remove/undo the highlight of the current selected link
				gotoxy(
				  link_data[curr_link_tab_idx].cursor_x,
					link_data[curr_link_tab_idx].cursor_y
				);				
				PRINT_OUT_RAW('>');

        // decement to the prior tab link
				if (curr_link_tab_idx == 0)
				{
					curr_link_tab_idx = link_data_idx-1;
				}
				else
				{
				  --curr_link_tab_idx;
				}
        goto init_tab_link;
			}
						
			else if (
				(ch_result == KEY_TAB)  // cycle to the next available link				
				|| (ch_result == KEY_DOWN)
			)
			{
				// remove/undo the highlight of the current selected link
				gotoxy(
				  link_data[curr_link_tab_idx].cursor_x,
					link_data[curr_link_tab_idx].cursor_y
				);				
				PRINT_OUT_RAW('>');

        // increment to the next tab link
				++curr_link_tab_idx;
				if (curr_link_tab_idx >= link_data_idx)
				{
					curr_link_tab_idx = 0;				
				}
				
init_tab_link:				
				// show marker of the new selected link
				gotoxy(
				  link_data[curr_link_tab_idx].cursor_x,
					link_data[curr_link_tab_idx].cursor_y
				);				
				PRINT_OUT_RAW('*');
				
				ch_result = 0;
			}
		}
		
		if (ch_result != 0) break;

		get_mouse_textmode();
		show_link_target();

		if ((mouse_buttons & 0x01) == 0x01)  // yep, biased on left-click!
		{
			// check if pressed on a link
			check_for_link_selected();
		}
		
		if ((mouse_buttons & 0x02) == 0x02)  // right click to press SPACE
		{
			ch_result = ' ';
		}
		mouse_buttons = 0;  //< to help avoid mouse button "stutter"

		if (ch_result != 0) break;  // if a link was selected, it "virtually" presses space to proceed (to the linked location)

		//gotox(8);
		//printf("%u %u %u ", mouse_x, mouse_y, mouse_buttons);
		
	} while (TRUE);
	//printf("\r      \r");  // only if not doing the clear screen
	//gotoxy(program_config_ptr->margin_left, res_yM1);
	
	if (ch_result == KEY_ESC)
	{
    PRINT_OUT_RAW('\n');
		//printf("\n");
		return MENU_ESC;
	}	

	CLRSCR;
	// clear all current links; going to "the next page" is sort of like a total restart,
	// just we don't adjust the file pointer and just keep reading the file from where we're at.
	// This way not a lot of RAM is needed to store up or buffer ahead and file content.  But will
	// make searching for tags pretty inefficient.  Don't expect any huge .nfo files tho.
	link_data_idx = 0; 	
	visible_height = 0;
	visible_width = program_config_ptr->margin_left;
	
  if (
	  (ch_result == KEY_BRACKET_LEFT)
	  || (ch_result == KEY_BRACKET_RIGHT)
	)
	{
		buffer_idx = 0;
		return MENU_SKIP;
	}
	if (goto_tag_str[0] != '\0')
	{
		return MENU_TAG;
	}
	if (new_file == TRUE)
	{
		new_file = FALSE;
		return MENU_NEW_FILE;
	}
	
	return MENU_CONTINUE;
}

char default_filename[60] = "index.nfo\0";
// the above is given 60 since during links, the string that argv[1] points to is modified to the target
// so in case those targets are long, the 60 pre-allocated room for that

char g_in_ch[4];
char freadch(char fn)
{	
	char n_bytes;
	n_bytes = cbm_read(fn, g_in_ch, 1);  // -1 if error
	if (n_bytes == 1)
	{
		return g_in_ch[0];
	}
  // else: if 0 bytes or any error, eitherway interpret as EOF
	g_eof = TRUE;
	return 0xFF;
}

#define CBM_READ_BUFFER_LEN 20
char cbm_read_buffer[CBM_READ_BUFFER_LEN];
void do_some_read(char lfn)
{	
	char n_bytes;
	n_bytes = cbm_read(lfn, cbm_read_buffer, CBM_READ_BUFFER_LEN);  // -1 if error
	// make sure there is a null terminator...
	if (n_bytes < CBM_READ_BUFFER_LEN)
	{
		cbm_read_buffer[n_bytes] = '\0';
	}
	else
	{
		cbm_read_buffer[CBM_READ_BUFFER_LEN-1] = '\0';
	}
}

#define golden_ram_start 0x0400
#define golden_ram_end   0x07ff

void check_in_ram_arguments()
{
  program_config_ptr = (Program_config*)golden_ram_start;

  if (
    (program_config_ptr->header[0] == 'x')
    && (program_config_ptr->header[1] == 'i')
    && (program_config_ptr->header[2] == 'n')
    && (program_config_ptr->header[3] == 'f')
    && (program_config_ptr->header[4] == 'o')
    && (program_config_ptr->header[5] == '0')
  )
  {
    /*
    printf("XINFO found in RAM!\n");
    printf("%d %d %d %d\n",
      program_config_ptr->margin_top,
      program_config_ptr->margin_bottom,
      program_config_ptr->margin_left,
      program_config_ptr->margin_right
    );
    printf("%d %d %d\n",
      program_config_ptr->n_fn,
      program_config_ptr->n_device,
      program_config_ptr->n_sec_addr
    );

    exit(-12);
    */
  }
  else
  {
    //printf("XINFO using defaults\n");
    program_config_ptr = &program_config_default;
  }
}

char hex2dec(char* value)
{
  char result = 0;
  //if (value[0] == 0) do_nothing
  if (value[0] == '1') result = 16;
  else if (value[0] == '2') result = 16*2;
  else if (value[0] == '3') result = 16*3;
  else if (value[0] == '4') result = 16*4;
  else if (value[0] == '5') result = 16*5;
  else if (value[0] == '6') result = 16*6;
  else if (value[0] == '7') result = 16*7;
  else if (value[0] == '8') result = 16*8;
  else if (value[0] == '9') result = 16*9;
  else if (value[0] == 'a') result = 16*10;
  else if (value[0] == 'b') result = 16*11;
  else if (value[0] == 'c') result = 16*12;
  else if (value[0] == 'd') result = 16*13;
  else if (value[0] == 'e') result = 16*14;
  else if (value[0] == 'f') result = 16*15;
  // else - uh oh, invalid hex (we'll just assume 0)
  
  //if (value[1] == '0' do_nothing
  if (value[1] == '1') result += 1;
  else if (value[1] == '2') result += 2;
  else if (value[1] == '3') result += 3;
  else if (value[1] == '4') result += 4;
  else if (value[1] == '5') result += 5;
  else if (value[1] == '6') result += 6;
  else if (value[1] == '7') result += 7;
  else if (value[1] == '8') result += 8;
  else if (value[1] == '9') result += 9;
  else if (value[1] == 'a') result += 10;
  else if (value[1] == 'b') result += 11;
  else if (value[1] == 'c') result += 12;
  else if (value[1] == 'd') result += 13;
  else if (value[1] == 'e') result += 14;
  else if (value[1] == 'f') result += 15;  
  // else - uh oh, invalid hex (we'll just assume 0)
  
  return result;
}

void main(int argc, char** argv)
{
	char link_len;
	char in_ch;
	char byte_value;
	char* chr_ptr;
	//FILE* f;  // fopen doesn't support long filenames, using cbm_open instead
	
	__asm__("sec");
	__asm__("jsr $ff5f");  // get screen mode
	__asm__("sta %v", orig_video_mode);
	curr_video_mode = orig_video_mode;	

  file_still_open = FALSE;
	
  program_config_default.margin_top = 2;
  program_config_default.margin_bottom = 2;
  program_config_default.margin_left = 2;
  program_config_default.margin_right = 2;
  program_config_default.n_fn = 1;
  program_config_default.n_device = 8;
  program_config_default.n_sec_addr = 6;
  program_config_default.parse_error = 0;
  program_config_default.parsed_x = 0;
  program_config_default.parsed_y = 0;
    
  check_in_ram_arguments();
	
	if (program_config_default.margin_right < 1)
	{
		program_config_ptr->parse_error = 0x0A;
		exit(ERR_MARGIN_RIGHT);
	}

	if (
		(argc < 2)
	)
	{
		argv[1] = default_filename;  // default to the index.nfo file
		//printf("xmanual <topic>.nfo\n");
		//exit(ERR_NO_ARG);
	}

	if (argc >= 6)
	{
		// some filename must be specified in order to override these
		program_config_ptr->margin_top = atoi(argv[2]);
		program_config_ptr->margin_bottom = atoi(argv[3]);
		program_config_ptr->margin_left = atoi(argv[4]);
		program_config_ptr->margin_right = atoi(argv[5]);
	}
	
	if (argc >= 9)
	{
		program_config_ptr->n_fn = atoi(argv[6]);
		program_config_ptr->n_device = atoi(argv[7]);
		program_config_ptr->n_sec_addr = atoi(argv[8]);
	}

  goto_tag_str[0] = '\0';  // set to not-doing-tag search
    
start_over:

	ENABLE_ISO_MODE;
	get_screen_resolution();
	reset_mouse();
	current_page = 0;

	//f = fopen(argv[1], "r");
	//if (f == 0)
	g_eof = FALSE;
	g_f = cbm_open(program_config_ptr->n_fn, program_config_ptr->n_device, program_config_ptr->n_sec_addr, argv[1]);
	if (g_f != 0)
	{
    program_config_ptr->parse_error = 0x08;
		//printf("not found [%s]\n", argv[1]);
		exit(ERR_FILE_NOT_FOUND);
	}
	//file_idx = 0;	
	arg1_ptr = argv[1];
	
	
	// already checked g_f earlier, no reason to check it again - assume file is successfuly opened by this point	
	g_f = cbm_open(15, program_config_ptr->n_device, 15, "");  //< CBM convention, we have to check the channel 15 status or else subsequent reads might fail
	if (g_f == 0)
	{
	  do_some_read(15);
		cbm_close(15);  
		if (strstr(cbm_read_buffer, "ok") == 0)
		{
      program_config_ptr->parse_error = 0x09;
			//printf("file error [%s] [%s]\n", argv[1], cbm_read_buffer);
		  exit(ERR_FILE_ISSUE);
		}
	}
	cbm_close(15);  
	// don't have to actually do anything with fn15/channel 15...	
	file_still_open = TRUE;
	{
		program_config_ptr->parsed_x = 0;
		program_config_ptr->parsed_y = 1;

		visible_width = program_config_ptr->margin_left;
		visible_height = 0;
		word_wrap_mode = TRUE;
    //printf("wmt");

		while (TRUE)
		{
			in_ch = freadch(program_config_ptr->n_fn);
			/*in_ch = fgetc(f);*/  ++program_config_ptr->parsed_x;  //++file_idx;
			if (g_eof == TRUE) // (feof(f))
			{
				goto early_eof;
			}
			
			if (in_ch == '\n')
			{
				++program_config_ptr->parsed_y;
				program_config_ptr->parsed_x = 0;
			}
			else if (in_ch == '\r')
			{
				// do nothing
			}
			else if (in_ch == '<')
			{
				// Skip any whitespace that happens to be after the token marker
				while(TRUE)
				{
					in_ch = freadch(program_config_ptr->n_fn);
					/*in_ch = fgetc(f);*/  ++program_config_ptr->parsed_x;  //++file_idx;
					if (g_eof == TRUE)  // (feof(f))
					{
						goto early_eof;
					}
					if (in_ch != ' ')
					{
						break;
					}
					// skip whitespace here
				}
				
				// See if a "double token marker" is present...
				if (in_ch == '<')  // should be "<<" case
				{
					// double-token detected, print as-is
					g_ch = '<';  VIRTUAL_OUT(IS_VISIBLE);  ASSESS_OUTPUT(FALSE);
				}
				else if ((in_ch >= 'a') && (in_ch <= 'z'))
				{
					// start of some command token
					do
					{
						cmd_stack[cmd_stack_idx] = in_ch;
						++cmd_stack_idx;
						
						in_ch = freadch(program_config_ptr->n_fn);
						/*in_ch = fgetc(f);*/  ++program_config_ptr->parsed_x;  //++file_idx;
						if (g_eof == TRUE) // (feof(f))
						{
              program_config_ptr->parse_error = 0x01;
							//printf("eof before parsed end of command token (l%d, c%d)\n", program_config_ptr->parsed_y, program_config_ptr->parsed_x);
							goto early_eof;
						}
					} 
					while ((in_ch >= 'a') & (in_ch <= 'z'));
					cmd_stack[cmd_stack_idx] = 0;
					cmd_stack_idx = 0;  // reset for next cmd
					
					if (in_ch == ':')
					{
						while (TRUE)
						{
							in_ch = freadch(program_config_ptr->n_fn);
							/*in_ch = fgetc(f);*/  ++program_config_ptr->parsed_x;  //++file_idx;
							if (g_eof == TRUE)  // (feof(f))
							{
                program_config_ptr->parse_error = 0x02;
								//printf("eof before parsed command token value (l%d, c%d)\n", program_config_ptr->parsed_y, program_config_ptr->parsed_x);
								goto early_eof;
							}
							if (in_ch == ' ')
							{
								// this allows whitespaces in the link target to make the input file alignment look
								// nicer and easier to align menu-like text, but for our purposes all spaces are ignored
								// in the link and tag targets.   This may end up confusing users, but that's how it is
								// for now.
								in_ch = '\0';
								//printf("link desc may not contain space (l%d, c%d)\n", parsed_y, parsed_x);
								//goto early_eof;
							}
							if (in_ch == '>')
							{
								// done parsing this control
								value_stack[value_stack_idx] = 0;
								break;
							}
							if (in_ch != '\0')
							{
							  value_stack[value_stack_idx] = in_ch;
							  ++value_stack_idx;
							}
						}
						value_stack_idx = 0;  // reset for next command
						
						//printf("[%s][%s]==", cmd_stack, value_stack);
						
						if (strstr(cmd_stack, "con") != 0)
						{
							// CONTROL COMMAND
              byte_value = hex2dec(value_stack);
							//sscanf(value_stack, "%X", &byte_value);  //< TBD: Need to replace this. Don't need ALL of sscanf just for a hex conversion.
							
							//printf("[%d]\n", byte_value);
							
							if (byte_value == KEY_ENTER)
							{
								// RETURN
								g_ch = byte_value;  VIRTUAL_OUT(NOT_VISIBLE);
								ASSESS_OUTPUT(TRUE);
							}
							else if (byte_value == CMD_WRAP_OFF)  // word wrap disable for this line
							{
								// non-printed control character
								word_wrap_mode = FALSE;
								g_ch = byte_value;  VIRTUAL_OUT(NOT_VISIBLE);
                //printf("wmf");
							}
							else if (byte_value == CMD_MENU)  // force menu
							{
								// non-printed control character
								//visible_height = res_yM1;								
								if (goto_tag_str[0] == '\0') goto forced_menu;
								// else ignore the FORCE MENU command, since we're searching for a tag
							}
							else if (byte_value == CMD_LINE_SEPERATOR)
							{
								if (visible_width >= res_x)
								{
									// the line is already too long for any more divider content
								}
								else
								{
									g_i3 = (res_x - visible_width);
									g_ch = '-';  
									while (g_i3 > 0)
									{ 
										VIRTUAL_OUT(IS_VISIBLE);  ASSESS_OUTPUT(FALSE);
										--g_i3;
									}
									word_wrap_mode = TRUE; 
									g_ch = KEY_ENTER;
									VIRTUAL_OUT(NOT_VISIBLE);  ASSESS_OUTPUT(TRUE);
								}
							}
							else if (
								(byte_value == 0x1C)  // red
								|| (byte_value == 0x05)  // white
								|| (byte_value == 0x1E)  // green
								|| (byte_value == 0x1F)  // blue
								|| (byte_value == 0x81)  // orange
								|| (byte_value == 0x90)  // black
								|| ((byte_value >= 0x95) && (byte_value <= 0x9C))   // brown .. purple
								|| (byte_value == 0x9E)  // yellow
								|| (byte_value == 0x9F)  // cyan
								|| (byte_value == 0x01)  // REVERSE
							)
							{
								// color change but does not take screen space
								g_ch = byte_value;  VIRTUAL_OUT(NOT_VISIBLE);
							}
							else if (  // some printable control output character
								is_visible(byte_value)
							)
							{
								// some printable/visible character (takes screen space)
								g_ch = byte_value;  VIRTUAL_OUT(IS_VISIBLE);  ASSESS_OUTPUT(FALSE);
							}
						}
						else if ((strstr(cmd_stack, "xli") != 0) && (goto_tag_str[0] == '\0'))
						{
							// external link command
							chr_ptr = strchr(value_stack, ',');
							if (chr_ptr == 0)
							{
                program_config_ptr->parse_error = 0x03;
								//printf("xlink missing command for link description (l%d, c%d)\n", program_config_ptr->parsed_y, program_config_ptr->parsed_x);
								goto early_eof;
							}
							*chr_ptr = 0;

							strcpy(link_data[link_data_idx].link_ref, value_stack);
							link_data[link_data_idx].link_type = LT_EXTERNAL;  // XLINK (external)
							
							g_ch = link_code[link_data_idx];  VIRTUAL_OUT(NOT_VISIBLE);  // use a non-printable character as a marker for start of an external link							
							link_len = 1;  // this becomes the LINK_LENGTH  
							g_ch = '>';
							VIRTUAL_OUT(IS_VISIBLE);  ASSESS_OUTPUT(FALSE);
							g_ch = 0x01;  VIRTUAL_OUT(NOT_VISIBLE);  // swap colors							
							while(TRUE)
							{
								++chr_ptr;
								if (*chr_ptr == 0)
								{
									break;
								}
								g_ch = *chr_ptr;  VIRTUAL_OUT(IS_VISIBLE);  ASSESS_OUTPUT(FALSE); 
								++link_len;
							}
							g_ch = 0x01;  VIRTUAL_OUT(NOT_VISIBLE);  // swap colors
							
							link_data[link_data_idx].link_len = (link_len-1);
							++link_data_idx;
						}
						else if ((strstr(cmd_stack, "tli") != 0) && (goto_tag_str[0] == '\0'))
						{
							// tag link command
							chr_ptr = strchr(value_stack, ',');
							if (chr_ptr == 0)
							{
                program_config_ptr->parse_error = 0x04;
								//printf("tlink missing command for link description (l%d, c%d)\n", program_config_ptr->parsed_y, program_config_ptr->parsed_x);
								goto early_eof;
							}
							*chr_ptr = 0;

							strcpy(link_data[link_data_idx].link_ref, value_stack);
							link_data[link_data_idx].link_type = LT_TAG;
							
              g_ch = link_code[link_data_idx];  VIRTUAL_OUT(NOT_VISIBLE);  // use a non-printable character as a marker for start of a tag link							
							link_len = 1;  // this becomes the LINK_LENGTH  
							g_ch = '>';
							VIRTUAL_OUT(IS_VISIBLE);  ASSESS_OUTPUT(FALSE);
							g_ch = 0x01;  VIRTUAL_OUT(NOT_VISIBLE);  // swap colors							
							while(TRUE)
							{
								++chr_ptr;
								if (*chr_ptr == 0)
								{
									break;
								}
								g_ch = *chr_ptr;  VIRTUAL_OUT(IS_VISIBLE);  ASSESS_OUTPUT(FALSE);
								++link_len;
							}
							g_ch = 0x01;  VIRTUAL_OUT(NOT_VISIBLE);  // swap colors
							
							link_data[link_data_idx].link_len = (link_len-1);
							++link_data_idx;
						}
						else if (strstr(cmd_stack, "tag") != 0)
						{
							// specify a tag
							//strcpy(tag_data[tag_data_idx].tag_name, value_stack);
							//tag_data[tag_data_idx].offset = file_idx;
							//++tag_data_idx;
							
							if (strcmp(goto_tag_str, value_stack) == 0)
							{
								// end search for tag mode
								goto_tag_str[0] = '\0';
							}
						}
					}
					else
					{
						// INVALID CONTROL
            program_config_ptr->parse_error = 0x05;
						//printf("control not followed by colon (:) (l%d, c%d)\n", program_config_ptr->parsed_y, program_config_ptr->parsed_x);
						goto early_eof;
					}
				}
				else
				{
					// INVALID CONTROL
          program_config_ptr->parse_error = 0x06;
					//printf("expected a command starting with a-z (not [%c]) (l%d, c%d)\n", in_ch, program_config_ptr->parsed_y, program_config_ptr->parsed_x);
					goto early_eof;
				}
				
			}
			else
			{
				if (
					is_visible(in_ch)
				)
				{
					// some regular visible output
					g_ch = in_ch;  VIRTUAL_OUT(IS_VISIBLE);  ASSESS_OUTPUT(FALSE);
				}
				else
				{
					// assume some non-printable character
				}
			}

			if (visible_height >= res_yM1)
			{
forced_menu:
				g_i3 = handle_pause();
				if (g_i3 > 0)  // 0 is a continue, anything else is either a link selection or ESC
				{
					goto early_eof;
				}
			}
		}  // end while(TRUE)

early_eof:
		ASSESS_OUTPUT(TRUE);
		cbm_close(program_config_ptr->n_fn);
		file_still_open = FALSE;
		//fclose(f);

		if (g_i3 == MENU_CONTINUE)  // handle a "natural pause" due to end of file
		{
			g_i3 = handle_pause();
		}
		if (g_i3 == MENU_ESC)
		{			
			// handle_pause already did a \n if they pressed ESCAPE
			DISABLE_ISO_MODE;  //< what if they were in ISO mode themselves already?  and don't really want to clear the screen on exit, TBD...
			
			__asm__("clc");
			__asm__("lda %v", orig_video_mode);
			__asm__("jsr $ff5f");  // set original screen mode		
			
			exit(ERR_ESCAPE);
		}
		// else MENU_NEW_FILE or MENU_TAG or MENU_SKIP...

		// g_i3 == 2 would be to start tag mode, which we can start over
		// or end of file was really reached, so automatically just start over
		g_i3 = MENU_CONTINUE;  //< necessary in case we straight-shot to end of file during next run
		goto start_over;
		
		//printf("file closed\n");
	}  // end if (f)
	
//	printf("end of line.\n");
}
