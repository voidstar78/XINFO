**XINFO** for the Commander X16
Public Domain - by v* Nov 2023

# Introduction
XINFO is a text mode "mark up document viewer" for the Commander X16.  The colorized mark-up can work for any system that uses the PETSCII color code assignments (as described in https://sta.c64.org/cbm64pet.html ).  However, what makes XINFO specific to the X16 is that it has mouse-enabled hyperlink support.  This relies on a KERNAL call that is specific to the X16 system (to query for mouse coordinates while in text mode).

As a convention, proposing XINFO file extensions be ".nfo".  However, they are normal text files that can be edited with X16EDIT, notepad++, etc.  XINFO will enable the X16 ISO mode when it is started, allowing use of braces ( { } ) and other "normal characters" when creating content from a modern PC.  ISO mode is exited when you press ESCAPE and exit XINFO.

# Supported Markup Tokens
Following a convention similar to HTML, XINFO will use brackets ( < > ) as the token identifier.  But besides that, XINFO has no association with HTML (e.g. no BODY or H header tags, etc.).  XINFO has four tokens:

## CONTROL token
```
<CONTROL:xx>
or
<CON:xx>
```

xx is a two character hex value that corresponds to the PETSCII codes as presented here:  https://sta.c64.org/cbm64pet.html
Only a few codes are supported, mainly ENTER and FOREGROUND COLOR changes:
```
0D = ENTER/RETURN

FF = Disable word wrap mode for the current row (i.e. until code 0D is encountered, then word wrap is enabled back on)
FE = Line Divider (dashes till end of current line) 
FD = Force Menu (causes the MENU to appear where this control token appears; will be ignored when linking to a tag; intent is to help "pause" at major content and more politely navigate to tags)
FC = Center current line (of the content up to the next newline); intended for short titles (can be used with CON:FF)

05 = WHITE
1C = RED
1E = GREEN
1F = BLUE
81 = ORANGE
90 = BLACK
95 = BROWN
96 = PINK
97 = DARK GREY
98 = GREY
99 = LIGHT GREEN
9A = LIGHT BLUE
9B = LIGHT GREY
9C = PURPLE
9E = YELLOW
9F = CYAN
```
NOTE: A trick about Line Divider -- if you want some spaces in the same row before the line break, toggle word wrap OFF first.  If word-wrap is on, the spaces will be detected as a wrap position and the line break will just end up on the next line.  So to prevent this, turn word wrap off, apply your spaces, then turn wrap back on.  Like this:
```<CON:FF>   <CON:FE><CON:FF>```

## External Link Token
```
<XLINK:target,title>
or
<XLI:target,title>
```

Define an external link.  Target can be relative or absolute path (relative to the CWD where XINFO was ran).  "target" is limited to 80 total characters including filename.  "title" is shown in reverse during XINFO runtime, as a left-mouse-clickable hyperlink to the specified target.

## Tag Link Token
```
<TLINK:tag,title>
or
<TLI:tag,title>
```

Define a tag link.  This is similar to external link, but the TAG is within the same file that is already opened.  This is typically as a "go back to top" feature, but can also be used to quickly go to various topics within the same file.  "tag" is the name of a specified tag.   "title" is what is shown to users that can be clicked on.

## Tag Target Token
```
<TAG:tag>
```

Defines a target tag called "tag" to be used in TLINK commands.


# Running XINFO
There are a few ways to use XINFO.

#1) If you just load the XINFO.PRG and RUN, XINFO will use the following defaults:
Use margins of 2 spaces on each side, file #1 of device #8, and look for INDEX.NFO in the current folder.   If INDEX.NFO is not found, the program exits back to BASIC.

#2) On startup, XINFO will look for the word "XINFO0" starting at $0400 ("golden RAM").  If that sequence is found, then it is assumed this region of memory is being used to convey parameters to XINFO.  The XINFOCFG.PRG is included as a sample BASIC program that can be used to set the program configuration into this memory region.  That example can become part of an AUTOBOOT.X16.  Or adjust the configuration and run XINFOCFG.PRG before loading and running XINFO.PRG itself.   The adjustable parameters are the margin and expected file index/device to use.  Note that the initial/default filename of INDEX.NFO cannot be adjusted with this method.

#3) XINFO was developed using cc65, a C-compiler for 6502-based systems.  As a convention, cc65 supports passing command line arguments using a REM statement when invoking the runtime of the program.   This method is the only way to deviate from starting up with INDEX.NFO.  A typical load/run of XINFO looks like this:

Run XINFO with default margins on all sides set to 2:
```
LOAD "XINFO.PRG"
RUN:REM EXAMPLE.NFO
```

Run XINFO with no margins:  (note that the RIGHT MARGIN must be at least 1)
```
LOAD "XINFO.PRG"
RUN:REM TUTORIAL.NFO 0 0 0 1
```

The margin arguments are in the order [top] [bottom] [left] [right] [id] [device] [sub].  For example, to run with top margin of 5 spaces, bottom margin of 4 spaces, left margin of 3 spaces, right margin of 2 spaces, run XINFO like this:
```
LOAD "XINFO.PRG"
RUN:REM TUTORIAL.NFO 5 4 3 2
```

To also configure to look for the startup file on a different device (in this example, file index 1 on device 9, using sub channel 6):
```
LOAD "XINFO.PRG"
RUN:REM TUTORIAL.NFO 3 3 3 3 1 9 6
```

If no filename is specified, XINFO will look for **INDEX.NFO** by default

# Error Reporting
To keep the PRG as small as possible, printf is not being used within XINFO and there are no convenient "plain English" error messages.  Instead error codes are reported in the "XINFO configuration" memory region.  This only applies when using method #2 to run XINFO (where the program configuration is set starting at $0400).  The error code is the 14th offset from $0400, so at $040E.  Following this address at $040F and $0410 is 16-bit used to express the x/y location of the error in the original input file (in the case of parsing errors).

The error codes are as follows (hex):
- 0x01: input file parsing error: EOF (end of file) was encountered before the proper closing of a command token (control code portion)
- 0x02: input file parsing error: EOF (end of file) was encountered before the proper closing of a command token (value portion)
- 0x03: input file parsing error: XLINK command missing , to start the target definition
- 0x04: input file parsing error: TLINK command missing , to start the target definition
- 0x05: input file parsing error: control code not followed by a colon :
- 0x06: input file parsing error: token symbol was found but not followed by alphabet character A-Z
- 0x07: internal error, the buffered visible width has become wider than the available display.  This indicates a coding error related to some unusual word-wrap mishap.
- 0x08: error OPENING the specified startup file (such as a device access error)
- 0x09: error specified startup file is not OK (typically a file-not-found error for the specified startup file, which is INDEX.NFO by default)
- 0x0A: right margin is less than 1 (i.e. 0).  Due to KERNAL issues related to wrapping at the physical edge of the screen in all the available text modes (and not-wrapping when wrap mode is off), the right side margin must be at least 1.



# XINFO Usage
XINFO will read the specified input file.  When it has buffered up "one row worth of displayable text" it will output that row, applying any non-visible CONTROL codes along the way (color changes).  It will also word wrap as appropriate to the current screen text-mode size (unless word-wrap with signaled as OFF).   The typical \n or \n\r in the input file do not matter - they are treated as non-printable characters and not shown.  You can only newline in XINFO by specifying <CON:0D>

At the end of a full text screen (or if the end of the file is encountered), XINFO will pause and show a [MENU] choice.  At this prompt, there are a few options:
- SPACE: Go to the next page.  If you are at the end of the file, pressing SPACE will just restart back to the top of the file.  Between each page the screen is cleared.
- ESCAPE: Exit XINFO (exits ISO mode, which also clears the screen)
- Mouse movements are monitored during the MENU display, so mousing over a link will show the target of that link.  Left click to select the link.


# Limitations

To bound the RAM usage, XINFO does not open the entire specified file at once.  It parses "about a screen worths" of content at a time.  Within that content, the following limits apply:

- up to 30 "visible" links (XLINK or TLINK)
- Tag target can be up to 40 characters
- External link targets can be to 80 characters

This is all adjustable in the .c source code, just depends on how much RAM to be reserved upfront.  To keep code compact, so far avoiding using malloc and just using fixed size pre-allocated arrays.

Since "visible" links is confusing, here is a little explanation:  If the user is using 80x60 they can have more links visible at a time than at smaller resolutions.  You'll need to arrange the .NFO content such that, in the worse case at 80x60, only 30 links can be presented at a time on the same page.  Each link counts, even if they go to the same target.  To make things easier, I'd just suggest the following: only do up to 30 links per file.  If you need more then that, break the content up into multiple files.  But the point here it is not really a per-file limit, but a "per what the user see's on the screen" limit (so you can technically go around the 30-link per file limit by having huge gaps in the content, but that's silly- just go with multiple files).

Another limitation is that after a cc65-built program runs once, you can't run it again until you re-load it.  So for now, when you exit from XINFO, you must do LOAD "XINFO.PRG" again before running it again.



