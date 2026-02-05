# mfg

maanoo's find and grep

Search for files in a directory hierarchy optionally matching a pattern and optionally print their content that match a pattern.

Supports pattern type selection, bfs file search, echo filename if greps, multicolor matches, handles matches in very long lines, builtin directory excludes.

Implemented with `liburing` and `memmem`.

![Screenshot](https://private-user-images.githubusercontent.com/6997990/545290778-4276d67e-e942-466e-bb95-27c02a90609c.png?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3NzAyNTAwNzcsIm5iZiI6MTc3MDI0OTc3NywicGF0aCI6Ii82OTk3OTkwLzU0NTI5MDc3OC00Mjc2ZDY3ZS1lOTQyLTQ2NmUtYmI5NS0yN2MwMmE5MDYwOWMucG5nP1gtQW16LUFsZ29yaXRobT1BV1M0LUhNQUMtU0hBMjU2JlgtQW16LUNyZWRlbnRpYWw9QUtJQVZDT0RZTFNBNTNQUUs0WkElMkYyMDI2MDIwNSUyRnVzLWVhc3QtMSUyRnMzJTJGYXdzNF9yZXF1ZXN0JlgtQW16LURhdGU9MjAyNjAyMDVUMDAwMjU3WiZYLUFtei1FeHBpcmVzPTMwMCZYLUFtei1TaWduYXR1cmU9MDQ2Yzc3M2ZiMTk2MjYzNjI1YTBmMzhlNzM0Y2JjZmQ4Mzc2OTZmYjFhNGM5MWU0MTEzYzA3YTU1NTllODE3ZCZYLUFtei1TaWduZWRIZWFkZXJzPWhvc3QifQ.Ptyt6FlyhKARbQGzhQCEa8BGJcJTWMdcqT4OvmFFKM0)

## Usage

### Options

```
       mfg [-bqpmta] FILE-TYPE [-ni] [NAME-PATTERN] [-nioma] [CONTENT-PATTERN]

DESCRIPTION
       mfg Search for files in a directory hierarchy optionally matching a pattern and
       optionally print their content that match a pattern.

       FILE-TYPE: a(all), f(files), d(directories), e(executables), t(textfiles), b(binary)

       NAME-PATTERN:
       - .
       - TEXT
       - [prefix|start|extension|end|fullname]: TEXT[,TEXT]

       CONTENT-PATTERN:
       - .
       - TEXT
       - [begin|start|end|]: TEXT
       - wrapped: START-TEXT END-TEXT
       - regex: <regex>

OPTIONS
   General options
       -b     BFS search
       -q     Output only the names of the files that the content pattern has matched
       -p     No color and no truncated output
       -m     Monochrome output
       -t     Table output
       -a     Don't search hidden files and directories

   Name options
       -n     Do not output the file names
       -i     Case sensitive file name pattern matching

   Content options
       -n     Do not output the matched content lines
       -i     Case sensitive content pattern matching
       -o     Output only the matched content
       -m     Multiline content pattern matching
       -a     Output also lines around the matched content lines

EXIT STATUS
       0      Successful program execution.
       1      Usage, syntax or configuration file error.
       2      Operational error.
```

## Install

```
git clone https://github.com/MaanooAk/mfg
cd mfg
make
sudo make install
```

Arch Linux: [AUR package](https://aur.archlinux.org/packages/mfg/).
