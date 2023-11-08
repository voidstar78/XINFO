#include <string.h>        //< memcpy, memset, strlen, strstr, strchr, strcpy
#include <stdlib.h>        //< Used for itoa, srand and exit
#include <stdio.h>         //< printf and fopen, fgetc
#include <conio.h>         //< cgetc, gotox/y

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

unsigned char ch_result;
#define GETCH_WAIT \
  __asm__("WAITL%s:", __LINE__); \
  __asm__("JSR GETIN"); \
  __asm__("CMP #0");   \
  __asm__("BEQ WAITL%s", __LINE__);  \
	__asm__("STA %v", ch_result);

#define GETCH \
  __asm__("JSR GETIN"); \
	__asm__("STA %v", ch_result);

#define ERR_NO_ARG -1
#define ERR_FILE_NOT_FOUND -2
#define ERR_VISIBILE_WIDTH -3
#define ERR_ESCAPE -4

#define MAX_LINKS_PER_PAGE 30
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

*/

char goto_tag_str[40];
char* arg1_ptr;

unsigned char res_x;
unsigned char res_y;
unsigned char res_xM1;
unsigned char res_yM1;
void get_screen_resolution()
{
	__asm__("jsr $ffed");  // get screen resolution	
	__asm__("stx %v", res_x);
	__asm__("sty %v", res_y);
		
	res_yM1 = res_y - 1;
	res_xM1 = res_x - 1;
}

unsigned char cursor_x;  // COLUMN (0-N)
unsigned char cursor_y;  // ROW (0-N)
void get_curr_xy()
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
	// NOTE: Reg.X contains mouse wheel info
	
	mouse_x = (((unsigned int)PEEK(0x0071) << 8) | (unsigned int)PEEK(0x0070)) >> 3;  // >>3 for /8, textmode 
	mouse_y = (((unsigned int)PEEK(0x0073) << 8) | (unsigned int)PEEK(0x0072)) >> 3;  // >>3 for /8, textmode 
}

#define BUFFER_LEN 1024U
char buffer[BUFFER_LEN];
unsigned int buffer_idx = 0;

char cmd_stack[100];
unsigned int cmd_stack_idx = 0;

char value_stack[100];
unsigned int value_stack_idx = 0;
unsigned char g_i;
unsigned char g_i2;
unsigned char g_i3;

// LT = link type
#define LT_EXTERNAL 0
#define LT_TAG 1
typedef struct
{	
	unsigned int link_len;  // length of the "link description" text (not the length of the link_ref)
	char link_ref[80];
	char link_type;  // 0 = external, 1 = tag
	unsigned char cursor_x;  // COLUMN
	unsigned char cursor_y;  // ROW
} Link_data;
Link_data link_data[MAX_LINKS_PER_PAGE];
unsigned int link_data_idx = 0;

typedef struct
{
	char tag_name[40];
	unsigned int offset;
} Tag_data;
Tag_data tag_data[MAX_LINKS_PER_PAGE];
unsigned int tag_data_idx = 0;

char g_ch;
unsigned int visible_width = 0;
unsigned int visible_height = 0;
void VIRTUAL_OUT(int visible)
{	
  buffer[buffer_idx] = g_ch;
	++buffer_idx;
	visible_width += visible;
}

void PRINT_OUT()
{	
  if (goto_tag_str[0] != '\0')
	{
		return;  // don't actually physically print, we're still in "search for tag" mode
	}

  if (g_ch == 0x0D)
	{
		++visible_height;
		goto auto_output;
	}
	
  for (g_i2 = 0; g_i2 < MAX_LINKS_PER_PAGE; ++g_i2)
	{
	  if (g_ch == link_code[g_i2])
		{
			get_curr_xy();
		  // store current cursor x/y, to help with link
		  link_data[g_i2].cursor_x = cursor_x;
		  link_data[g_i2].cursor_y = cursor_y;			
			
			//printf("<%d %d %d>", g_i2, cursor_x, cursor_y);
			break;
		}
	}
	if (g_i2 == MAX_LINKS_PER_PAGE)  // special non-printable link code not found
	{
auto_output:		
	  __asm__("lda %v", g_ch);
	  __asm__("jsr $FFD2");
	}
}

unsigned int parsed_x = 0;
unsigned int parsed_y = 1;

char is_visible(int ch)  // i.e. is this character code visible when printed to the console? 
{
	char result = FALSE;  // assume no...
	
	if (
	  ((ch >= 0x20) && (ch <= 0x7F))
	  || ((ch >= 0xA0) && (ch <= 0xFF))		
	)
	{
		result = TRUE;
	}
	
	return result;
}

void ASSESS_OUTPUT(char force_remaining)
{
	/*
	01234567890123456789  (model 20 column width)
	AAAAAAAAAAAAAAAAAAAA  (visible_width == res_x, no wrap)
	AAAAAAAAAAAAAAAAAAA   (space at end)
	A AAAAAAAAAAAAAAAAAA
	AAA AAAAAAA AAAA AAA
	AAAAAAAAAAA AAAAAAAA
	
	
	AAAAAAAAAAAAA AAaAAaAA  (found a wrap w/ controls)	
	              ^	
	
	*/
	unsigned int idx_orig;
	unsigned int tmp_idx;	
	
	if (visible_width > res_x)
	{
		printf("visible_width too large: %d", visible_width);
		exit(ERR_VISIBILE_WIDTH);
	}
		
	if (force_remaining || (visible_width == res_x))  // time to output a line
	{
		//printf("printing one line...[%d][%d]\n", force_remaining, buffer_idx);		
		//for (g_i = 0; g_i < buffer_idx; ++g_i)
		//{
		//	printf("%d ", buffer[g_i]);						
		//}
		//printf("\n");
		
		g_i = buffer_idx;
		while (TRUE)
		{
			--g_i;
			
      if (
			  (g_i == 0)
				|| (force_remaining != 0)
			)
      {
				//printf("[1,%d]", buffer_idx);
  			// just print the entire buffer content as-is (even if there is a space at the front)
				for (g_i = 0; g_i < buffer_idx; ++g_i)
			  {
				  g_ch = buffer[g_i]; PRINT_OUT();
			  }
				if (visible_width == res_x) ++visible_height;
			  buffer_idx = 0;
				visible_width = 0;
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
        //g_ch = buffer[g_i+1];  PRINT_OUT();				
				// the space effectively now becomes a newline
				g_ch = 0x0D;  PRINT_OUT();  
				
				// now write the remaining content that was past the space (accounting also for non-printables)
				idx_orig = tmp_idx+1;				
				visible_width = 0;
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
					get_curr_xy();
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
					ch_result = ' ';
				}
				else if (link_data[g_i].link_type == LT_EXTERNAL)
				{
					strcpy(arg1_ptr, link_data[g_i].link_ref);
					new_file = TRUE;
					ch_result = ' ';
				}
				// else code bug - unsupported link type
				
				break;
			}
		}
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
unsigned char handle_pause()
{
	printf("%c[menu]%c", 1, 1);  // %c used to output CHAR $01 which is interpreted to reverse fg/bg

	ch_result = 0;
	do
	{
		GETCH;
		if (ch_result != 0) break;
		
		get_mouse_textmode();
		
		if ((mouse_buttons & 0x01) == 0x01)
		{
			// check if pressed on a link
			check_for_link_selected();
		}		
		
		if (ch_result != 0) break;
		
		//gotox(8);
		//printf("%u %u %u ", mouse_x, mouse_y, mouse_buttons);
		
	} while (TRUE);
	//printf("\r      \r");  // only if not doing the clear screen
	
	if (ch_result == 27)
	{
		printf("\n");
		return MENU_ESC;
	}

	CLRSCR;
	link_data_idx = 0;  // clear all current links
	tag_data_idx = 0;
	visible_height = 0;
	
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
	
	goto_tag_str[0] = '\0';	
				
	if (argc < 2)
	{
		printf("xmanual <topic>.x16\n");
		exit(ERR_NO_ARG);
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
							sscanf(value_stack, "%X", &int_value);
							
							//printf("[%d]\n", int_value);
							
							if (int_value == 0x0D)
							{
								// RETURN								
								g_ch = int_value;  VIRTUAL_OUT(0);
								ASSESS_OUTPUT(TRUE);
							}
							else if (
								(int_value == 0x1C)  // red
								|| (int_value == 0x05)  // green
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
						else if (strstr(cmd_stack, "xli") != 0)
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
							link_data[link_data_idx].link_type = 0;  // XLINK (external)
							
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
						else if (strstr(cmd_stack, "tli") != 0)
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
							link_data[link_data_idx].link_type = 1;						
							
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
