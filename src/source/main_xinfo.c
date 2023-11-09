/*
voidstar - 2023

- left/right margins
- top/bottmo margins
- "block" mode toggle (enable/disable word wrap)
- show where link is going (like a URL)

*/
#include <string.h>        //< memcpy, memset, strlen, strstr, strchr, strcpy
#include <stdlib.h>        //< Used for itoa, srand and exit (mostly for exit in this one)
#include <stdio.h>         //< printf, sscanf and fopen, fgetc
#include <conio.h>         //< cgetc, gotox/y
/*
NOTE: printf is not needed by this code.  Only reason printf is used is as a
convenient way to show "parsing errors" during input of the .x16 document files.
i.e. to show the row/column of any parsing issues (like incomplete tokens, etc).
If we assume those inputs are "perfect" then no error checking is needed.
However, stdio.h is still needed for file-io stuffs.  But if we remove the printf
calls, that drops the code size at least 2KB or so.

I do use sscanf once, but only for a hex conversion, should be able to easily replace
that with a standalone function.  sscanf otherwise is another big waste of code space.
*/

#define TRUE  1
#define FALSE 0

#define POKE(addr,val)     (*(unsigned char*) (addr) = (val))
#define POKEW(addr,val)    (*(unsigned*) (addr) = (val))
#define PEEK(addr)         (*(unsigned char*) (addr))
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
unsigned char ch_result;
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

#define KEY_ESC 27
#define KEY_ENTER 13
#define CMD_WRAP_TOGGLE 0xFF
#define CMD_LINE_SEPERATOR 0xFE

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
unsigned char link_code[] = {
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

char goto_tag_str[40];  //< populated with search string of a tag link, change 0th element to \0 when done searching
char* arg1_ptr;  //< override first "command line argument" with new filename when doing external links

// Used for "parsing help" for when users make some syntax errors in specifying mark-up tokens.
unsigned int parsed_x;
unsigned int parsed_y;

unsigned char margin_top = 2;
unsigned char margin_bottom = 2;
unsigned char margin_left = 2;
unsigned char margin_right = 2;

unsigned char word_wrap_mode = TRUE;

unsigned char res_x;
unsigned char res_y;
unsigned char res_xM1;
unsigned char res_yM1;
void get_screen_resolution()
{
	__asm__("jsr $ffed");  // get screen resolution	
	__asm__("stx %v", res_x);
	__asm__("sty %v", res_y);
	
	res_x -= margin_right;  // induce an artificial margin on the right side of the screen
	res_y -= margin_bottom;

	res_yM1 = res_y - 1;  // important since "last row" reserved for [menu]
	res_xM1 = res_x - 1;
}

unsigned char cursor_x;  // COLUMN (0-N)
unsigned char cursor_y;  // ROW (0-N)
void get_cursor_xy()
{
	__asm__("sec");
	__asm__("jsr $fff0");
	__asm__("stx %v", cursor_y);  // set the ROW
	__asm__("sty %v", cursor_x);  // set the COLUMN
}

unsigned int mouse_x;  // column (0-N)
unsigned int mouse_y;  // row (0-N)
unsigned char mouse_buttons;
void get_mouse_textmode()
{
	__asm__("ldx #$70");  // where in zeropage to store mouse x/y
	__asm__("jsr $FF6B");  // mouse_get
	__asm__("sta %v", mouse_buttons);
	// NOTE: Reg.X contains mouse wheel info (not currently used by XINFO)

	mouse_x = (((unsigned int)PEEK(0x0071) << 8) | (unsigned int)PEEK(0x0070)) >> 3;  // >>3 for /8, textmode 
	mouse_y = (((unsigned int)PEEK(0x0073) << 8) | (unsigned int)PEEK(0x0072)) >> 3;  // >>3 for /8, textmode 
}

// Input buffer for reading from current file being viewed.  Should have enough room
// for a couple text mode rows (80x2 worse case) plus provision for non-printable color codes.
// Eats up some RAM, so keep it "not too large."
#define BUFFER_LEN 1024U
char buffer[BUFFER_LEN];
unsigned int buffer_idx = 0;

// Token commands during input, like "<CONTROL:"
char cmd_stack[20];
unsigned int cmd_stack_idx = 0;

// Value associated with the token; could be a full link target path plus description, so could get long
char value_stack[120];
unsigned int value_stack_idx = 0;

unsigned char g_i;   // general purpose loop counter
unsigned char g_i2;  // general purpose loop counter
unsigned char g_i3;  // general purpose loop counter

// LT = link type
#define LT_EXTERNAL 0
#define LT_TAG 1
typedef struct
{
	unsigned int link_len;  // length of the "link description" text (not the length of the link_ref)
	// note: we don't have to store the actual link description, since "what we're clicking on" isn't important,
	// we just need to know "where" to click

	char link_ref[60];  // link_ref can be a full path like "/manuals/good-stuff/longfilename.x16"
	char link_type;  // 0 = external, 1 = tag
	unsigned char cursor_x;  // COLUMN
	unsigned char cursor_y;  // ROW
} Link_data;
Link_data link_data[MAX_LINKS_PER_PAGE];
unsigned int link_data_idx = 0;

typedef struct
{
	char tag_name[40];  // arbitrarily allowing in-document tags to be up to 40 characters, but they'd probably be generally short like "TOP"
	unsigned int offset;
} Tag_data;
Tag_data tag_data[MAX_LINKS_PER_PAGE];
unsigned int tag_data_idx = 0;

/*
NOTE: The above two pre-allocated arrays start to eat up RAM quickly.
We could consider putting them in different BANKS, just to learn how that's done from cc65?
*/

char g_ch;
unsigned int visible_width;  // accounting for how much of the current text mode row has been used
unsigned int visible_height; // accounting for how many rows into the current text mode we've drawn on
// If the thing we're printing (g_ch) is intended to physically displayed, pass 1 to indicate
// it is visible and (virtually) account for that text mode width being consumed.  Otherwise,
// if it's a non-printable character (color change, etc) pass 0.
void VIRTUAL_OUT(int visible)
{
	// handle special case of the very first output, to apply the top and left margin
	if (
	  (visible == 1)
		&& (visible_width == margin_left)
		&& (visible_height == 0)
		&& (margin_top > 0)
	)
	{
		// this will induce visible_height to match the margin_top
		for (g_i2 = 0; g_i2 < margin_top; ++g_i2)
		{
			__asm__("lda #$0D");  // enter
			__asm__("jsr $ffd2");
			++visible_height;
		}
		for (g_i2 = 0; g_i2 < margin_left; ++g_i2)
		{
			__asm__("lda #$20");  // space
			__asm__("jsr $ffd2");
		}
	}
	
	buffer[buffer_idx] = g_ch;
	++buffer_idx;
	visible_width += visible;
}

void PRINT_OUT()
{
	unsigned char is_new_line = FALSE;  // assume not a new line
	
	if (goto_tag_str[0] != '\0')
	{
		return;  // don't actually physically print, we're still in "search for tag" mode
	}

	if (g_ch == KEY_ENTER)  // ENTER/RETURN
	{
		++visible_height;  // account for how many text-mode rows have been used up (to know when to show the MENU)
		is_new_line = TRUE;
		goto auto_output;
	}
	
	get_cursor_xy();
	if (
		(cursor_x == res_xM1)  // hit "edge" of the screen [res_x already includes right margin]
		&& (word_wrap_mode == FALSE)
	)
	{
		return;
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
		for (g_i2 = 0; g_i2 < margin_left; ++g_i2)
		{
			__asm__("lda #$20");  // space
			__asm__("jsr $ffd2");
		}
	}
}

char is_visible(int ch)  // i.e. is this character code visible when printed to the console? 
{
	char result = FALSE;  // assume no...
	
	if (
		((ch >= 0x20) && (ch <= 0x7F))
		|| ((ch >= 0xA0) && (ch < 0xFF))  // FF = word wrap toggle
	)
	{
		result = TRUE;
	}
	
	return result;
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
	unsigned int idx_orig;
	unsigned int tmp_idx;
	
	if (visible_width > res_x)
	{
		// We're doing like a 'pre-emptive' word wrap, not a look ahead.  This assessment
		// should be done during each individual addition to the current row.  If we buffer up beyond
		// the current row, funny stuff will happen.
		printf("visible_width too large: %d", visible_width);
		exit(ERR_VISIBLE_WIDTH);
	}

	if (force_remaining || (visible_width == res_x))  // time to output a line [note: visible_width already includes margin_left distance]
	{

		//printf("<%d %d %d %d>\n", force_remaining, visible_width, res_x, margin_left);

		//printf("printing one line...[%d][%d]\n", force_remaining, buffer_idx);
		//for (g_i = 0; g_i < buffer_idx; ++g_i)
		//{
		//	printf("%d ", buffer[g_i]);
		//}
		//printf("\n");

		g_i = buffer_idx;  // assume we need to output the entire current buffer
		while (TRUE)
		{
			--g_i;

			if (
				(g_i == 0)  // no suitable word wrap position found
				|| (force_remaining != 0)
				|| (word_wrap_mode == FALSE)
			)
			{
				//printf("[1,%d]", buffer_idx);
				// just print the entire buffer content as-is (even if there is a space at the front)
				for (g_i = 0; g_i < buffer_idx; ++g_i)  // this is mainly for the "force_remaining" case
				{
					g_ch = buffer[g_i]; PRINT_OUT();
				}

				get_cursor_xy();
				if (cursor_x == res_x)  // hit "edge" of the screen [res_x already includes right margin]
				{  
					if (margin_right == 0)
					{
						// we get a "natural newline" by CMDR-DOS due to hitting the actual physical edge of the text mode screen
						// that is, the cursor ends up the next line anyway without an actual CR.  Account for this manually...
						++visible_height;
						// manually apply the left margin
						for (g_i2 = 0; g_i2 < margin_left; ++g_i2)
						{
							__asm__("lda #$20");  // space
							__asm__("jsr $ffd2");
						}
					}
					else
					{
						// induce a new line by hitting the right side margin
						g_ch = KEY_ENTER; PRINT_OUT();
						visible_width = margin_left;
					}
				}

				buffer_idx = 0;  // flush the buffer to start working on the next row
				visible_width = margin_left;
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
				visible_width = margin_left;
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
					
					//for (g_i = 0; g_i < buffer_idx; ++g_i)
					//{
					//	printf("[%d]", buffer[g_i]);
					//}
					//printf("\n");
				}
				
				break;
			}
		}
		//exit(-7);
	}
	else
	{
		// not ready to print a line...
	}
}

unsigned char new_file = FALSE;
void check_for_link_selected()
{
	for (g_i = 0; g_i < link_data_idx; ++g_i)
	{
		if (mouse_y == link_data[g_i].cursor_y)
		{
			if (
				(mouse_x >= link_data[g_i].cursor_x)
				&& (mouse_x <= (link_data[g_i].cursor_x + link_data[g_i].link_len))
			)
			{
				/*
				gotox(8);
				printf("link [%c%s%c]", 0x9E, link_data[g_i].link_ref, 0x05);  // yellow  white
				do
				{
					printf(" ");
					get_cursor_xy();
					if (cursor_x == (res_xM1))
					{
						break;
					}
				} while (TRUE);
				*/
				
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
				
				break;
			}
		}
	}
}

void show_link_target()
{
	unsigned char temp_buffer[80];
	unsigned char max_len;
	
	for (g_i = 0; g_i < link_data_idx; ++g_i)
	{
		if (mouse_y == link_data[g_i].cursor_y)
		{
			if (
				(mouse_x >= link_data[g_i].cursor_x)
				&& (mouse_x <= (link_data[g_i].cursor_x + link_data[g_i].link_len))
			)
			{
				// show current link "path"
				gotox(6+margin_left);  // move past "[menu]"
				
				get_cursor_xy();
				max_len = res_xM1 - cursor_x - 2;  // - 2 is just so we don't write the link target reminder all the way to end of screen
				for (g_i2 = 0; g_i2 < max_len; ++g_i2)
				{
					temp_buffer[g_i2] = link_data[g_i].link_ref[g_i2];
				}
				temp_buffer[g_i2] = '\0';
				// note: the temp_buffer is needed just in case the link target is really link (longer than room on the screen to fit)
				// in that case we just show the first portion of it that would fit in the screen
				
				printf("[%c%s%c]", 0x9E, temp_buffer, 0x05);  // yellow  white
				// tbd: instead of white, we probably need to resume/revert to whatever the color was...
				
				// erase to the end of line (in case switching to showing shorter link)
				do
				{
					get_cursor_xy();
					if (cursor_x >= res_xM1)
					{
						break;
					}
					printf(" ");
				} while (TRUE);
				
				break;
			}
		}
	}
	if (g_i == link_data_idx)  // no link within the mouse x/y cursor
	{
		gotox(6+margin_left);  // move past "[menu]"
		
		// erase to the end of line
		do
		{
			get_cursor_xy();
			if (cursor_x >= res_xM1)
			{
				break;
			}
			printf(" ");
		} while (TRUE);
	}
}

// Return 0 for normal return 
// Return 1 for ESC pressed (exit)
// Return 2 for search for TAG mode enabled
// Return 3 for request to start over with new file
#define MENU_CONTINUE 0
#define MENU_ESC 1
#define MENU_TAG 2
#define MENU_NEW_FILE 3
unsigned char handle_pause()  // "pause" aka "the menu" (intermission between filled up screen pages)
{
	gotox(margin_left);
	printf("%c%c[menu]%c%c", 0x9C, 1, 1, 5);  // %c used to output CHAR $01 which is interpreted to reverse fg/bg (9c = purple, 5=white)
	// TODO: need a smarter way to adjust color of the menu, since reversing it makes it look like a clickable link
	// if we "force" a color, it could conflict with the current background color.

	ch_result = 0;
	do
	{
		GETCH;
		if (ch_result != 0) break;

		get_mouse_textmode();
		show_link_target();

		if ((mouse_buttons & 0x01) == 0x01)  // yep, biased on left-click!
		{
			// check if pressed on a link
			check_for_link_selected();
		}

		if (ch_result != 0) break;  // if a link was selected, it "virtually" presses space to proceed (to the linked location)

		//gotox(8);
		//printf("%u %u %u ", mouse_x, mouse_y, mouse_buttons);
		
	} while (TRUE);
	//printf("\r      \r");  // only if not doing the clear screen
	
	if (ch_result == KEY_ESC)
	{
		printf("\n");
		return MENU_ESC;
	}

	CLRSCR;
	// clear all current links; going to "the next page" is sort of like a total restart,
	// just we don't adjust the file pointer and just keep reading the file from where we're at.
	// This way not a lot of RAM is needed to store up or buffer ahead and file content.  But will
	// make searching for tags pretty inefficient.  Don't expect any huge .x16 files tho.
	link_data_idx = 0;  
	tag_data_idx = 0;
	visible_height = 0;
	visible_width = margin_left;

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

void main(int argc, char** argv)
{
	unsigned int i;
	unsigned int file_idx;
	int in_ch;
	int int_value;
	char* chr_ptr;
	FILE* f;

	goto_tag_str[0] = '\0';  // set to not-doing-tag search

	if (argc < 2)
	{
		printf("xmanual <topic>.x16\n");
		exit(ERR_NO_ARG);
	}

	if (argc == 6)
	{
		margin_top = atoi(argv[2]);
		margin_bottom = atoi(argv[3]);
		margin_left = atoi(argv[4]);
		margin_right = atoi(argv[5]);
	}

start_over:	
	ENABLE_ISO_MODE;
	get_screen_resolution();

	f = fopen(argv[1], "r");
	if (f == 0)
	{
		printf("not found [%s]\n", argv[1]);
		exit(ERR_FILE_NOT_FOUND);
	}
	file_idx = 0;	
	arg1_ptr = argv[1];
	
	if (f)
	{
		parsed_x = 0;
		parsed_y = 1;

		visible_width = margin_left;
		visible_height = 0;

		while (TRUE)
		{
			in_ch = fgetc(f);  ++parsed_x;  ++file_idx;
			if (feof(f))
			{
				goto early_eof;
			}
			
			if (in_ch == '\n')
			{
				++parsed_y;
				parsed_x = 0;
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
					in_ch = fgetc(f);  ++parsed_x;  ++file_idx;
					if (feof(f))
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
					g_ch = '<';  VIRTUAL_OUT(1);  ASSESS_OUTPUT(FALSE);
				}
				else if ((in_ch >= 'a') && (in_ch <= 'z'))
				{
					// start of some command token
					do
					{
						cmd_stack[cmd_stack_idx] = in_ch;
						++cmd_stack_idx;
						in_ch = fgetc(f);  ++parsed_x;  ++file_idx;
						if (feof(f))
						{
							printf("eof before parsed end of command token (l%d, c%d)\n", parsed_y, parsed_x);
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
							in_ch = fgetc(f);  ++parsed_x;  ++file_idx;
							if (feof(f))
							{
								printf("eof before parsed command token value (l%d, c%d)\n", parsed_y, parsed_x);
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
							sscanf(value_stack, "%X", &int_value);  //< TBD: Need to replace this. Don't need ALL of sscanf just for a hex conversion.
							
							//printf("[%d]\n", int_value);
							
							if (int_value == KEY_ENTER)
							{
								// RETURN
								g_ch = int_value;  VIRTUAL_OUT(0);
								ASSESS_OUTPUT(TRUE);
							}
							else if (int_value == CMD_WRAP_TOGGLE)  // word wrap TOGGLE
							{
								// non-printed control character
								word_wrap_mode = !word_wrap_mode;
							}
							else if (int_value == CMD_LINE_SEPERATOR)
							{
								g_i3 = (res_x - visible_width);
								g_ch = '-';  
								while (g_i3 > 0)
								{
								  VIRTUAL_OUT(1);  ASSESS_OUTPUT(FALSE);
									--g_i3;
								}
							}
							else if (
								(int_value == 0x1C)  // red
								|| (int_value == 0x05)  // white
								|| (int_value == 0x1E)  // green
								|| (int_value == 0x1F)  // blue
								|| (int_value == 0x81)  // orange
								|| (int_value == 0x90)  // black
								|| ((int_value >= 0x95) && (int_value <= 0x9C))   // brown .. purple
								|| (int_value == 0x9E)  // yellow
								|| (int_value == 0x9F)  // cyan
								|| (int_value == 0x01)  // REVERSE
							)
							{
								// color change but does not take screen space
								g_ch = int_value;  VIRTUAL_OUT(0);
							}
							else if (  // some printable control output character
								is_visible(int_value)
							)
							{
								// some printable/visible character (takes screen space)
								g_ch = int_value;  VIRTUAL_OUT(1);  ASSESS_OUTPUT(FALSE);
							}
						}
						else if ((strstr(cmd_stack, "xli") != 0) && (goto_tag_str[0] == '\0'))
						{
							// external link command
							chr_ptr = strchr(value_stack, ',');
							if (chr_ptr == 0)
							{
								printf("xlink missing command for link description (l%d, c%d)\n", parsed_y, parsed_x);
								goto early_eof;
							}
							*chr_ptr = 0;

							strcpy(link_data[link_data_idx].link_ref, value_stack);
							link_data[link_data_idx].link_type = LT_EXTERNAL;  // XLINK (external)
							
							g_ch = 0x01;  VIRTUAL_OUT(0);  // swap colors
							g_ch = link_code[link_data_idx];  VIRTUAL_OUT(0);  // use a non-printable character as a marker for start of an external link
							i = 0;  
							while(TRUE)
							{
								++chr_ptr;
								if (*chr_ptr == 0)
								{
									break;
								}
								g_ch = *chr_ptr;  VIRTUAL_OUT(1);  ASSESS_OUTPUT(FALSE); 
								++i;
							}
							g_ch = 0x01;  VIRTUAL_OUT(0);  // swap colors
							
							link_data[link_data_idx].link_len = (i-1);
							++link_data_idx;
						}
						else if ((strstr(cmd_stack, "tli") != 0) && (goto_tag_str[0] == '\0'))
						{
							// tag link command
							chr_ptr = strchr(value_stack, ',');
							if (chr_ptr == 0)
							{
								printf("tlink missing command for link description (l%d, c%d)\n", parsed_y, parsed_x);
								goto early_eof;
							}
							*chr_ptr = 0;

							strcpy(link_data[link_data_idx].link_ref, value_stack);
							link_data[link_data_idx].link_type = LT_TAG;
							
							g_ch = 0x01;  VIRTUAL_OUT(0);  // swap colors
							g_ch = link_code[link_data_idx];  VIRTUAL_OUT(0);  // use a non-printable character as a marker for start of a tag link
							i = 0;   
							while(TRUE)
							{
								++chr_ptr;
								if (*chr_ptr == 0)
								{
									break;
								}
								g_ch = *chr_ptr;  VIRTUAL_OUT(1);  ASSESS_OUTPUT(FALSE);
								++i;
							}
							g_ch = 0x01;  VIRTUAL_OUT(0);  // swap colors
							
							link_data[link_data_idx].link_len = (i-1);
							++link_data_idx;
						}
						else if (strstr(cmd_stack, "tag") != 0)
						{
							// specify a tag
							strcpy(tag_data[tag_data_idx].tag_name, value_stack);
							tag_data[tag_data_idx].offset = file_idx;
							++tag_data_idx;
							
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
						printf("control not followed by colon (:) (l%d, c%d)\n", parsed_y, parsed_x);
						goto early_eof;
					}
				}
				else
				{
					// INVALID CONTROL
					printf("expected a command starting with a-z (not [%c]) (l%d, c%d)\n", in_ch, parsed_y, parsed_x);
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
					g_ch = in_ch;  VIRTUAL_OUT(1);  ASSESS_OUTPUT(FALSE);
				}
				else
				{
					// assume some non-printable character
				}
			}

			if (visible_height >= res_yM1)
			{
				g_i3 = handle_pause();
				if (g_i3 > 0)
				{
					goto early_eof;
				}
//				for (g_i = 0; g_i < link_data_idx; ++g_i)
//				{
//					printf("%d %d\n", link_data[g_i].cursor_x, link_data[g_i].cursor_y);
//				}
			}
		}  // end while(TRUE)

early_eof:
		ASSESS_OUTPUT(TRUE);
		fclose(f);

		if (g_i3 == MENU_CONTINUE)  // handle a "natural pause" due to end of file
		{
			g_i3 = handle_pause();
		}
		if (g_i3 == MENU_ESC)
		{
			// handle_pause already did a \n if they pressed ESCAPE
			DISABLE_ISO_MODE;  //< what if they were in ISO mode themselves already?  and don't really want to clear the screen on exit, TBD...
			exit(ERR_ESCAPE);
		}
		// else MENU_NEW_FILE or MENU_TAG...

		// g_i3 == 2 would be to start tag mode, which we can start over
		// or end of file was really reached, so automatically just start over
		g_i3 = MENU_CONTINUE;  //< necessary in case we straight-shot to end of file during next run
		goto start_over;
		
		//printf("file closed\n");
	}  // end if (f)
	

//	printf("end of line.\n");
}
