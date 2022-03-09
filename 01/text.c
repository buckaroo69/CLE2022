#include <stdio.h>

struct fileStats
{
    unsigned long words;
    unsigned long startsVowel;
    unsigned long endsConsonant;
};

/*
receives the file pointer
reads x bytes based on utf 8 prefixes
all "word-ending" chars are returned as \0 instead and we skip "word-bridgers" -> bridge with recursion?
write down the masks at some point
*/
int readLetter(FILE *file)
{
    unsigned int readspot = 0;
    size_t readBytes = fread(&readspot, 1, 1, file);
    if (readBytes != 1)
        return EOF;
    if (!(readspot >> 7)) // leftmost byte is 0
        return readspot;
    else if (readspot <= 223 && readspot >= 192)
    { // leftmost 2 bytes are 1, followed by 0 -> 128 and 64 are in, 32 is not
        readspot <<= 8;
        fread(&readspot + 1, 1, 1, file); // TODO: I have NO CLUE whether this works
    }
    else if (readspot <= 239 && readspot >= 224)
    { // leftmost 3 bytes are 1, followed by 0 -> 16 is not in, 224 min
        readspot <<= 16;
        fread(&readspot + 1, 2, 1, file); // TODO: I have NO CLUE whether this works
    }
    else if (readspot <= 247 && readspot >= 240)
    { // leftmost 4 bytes are 1, followed by 0 -> 8 is not in and there's a 240 minimum
        readspot <<= 24;
        fread(&readspot + 1, 3, 1, file); // TODO: I have NO CLUE whether this works
    }
    else
    {
        perror("Invalid text found");
        return EOF;
    }

    return readspot;
}

/*
reads letter to check if it is a vowel
returns 1 if it is a vowel
returns 0 if not
*/
int isVowel(unsigned int letter)
{
    if (
        letter == 0x41                          // A
        || letter == 0x45                       // E
        || letter == 0x49                       // I
        || letter == 0x4f                       // O
        || letter == 0x55                       // U
        || letter == 0x61                       // a
        || letter == 0x65                       // e
        || letter == 0x69                       // i
        || letter == 0x6f                       // o
        || letter == 0x75                       // u
        || 0xc380 <= letter && letter <= 0xc383 // À Á Â Ã
        || 0xc388 <= letter && letter <= 0xc38a // È É Ê
        || 0xc38c <= letter && letter <= 0xc38d // Ì Í
        || 0xc392 <= letter && letter <= 0xc395 // Ò Ó Ô Õ
        || 0xc399 <= letter && letter <= 0xc39a // Ù Ú
        || 0xc3a0 <= letter && letter <= 0xc3a3 // à á â ã
        || 0xc3a8 <= letter && letter <= 0xc3aa // è é ê
        || 0xc3ac <= letter && letter <= 0xc3ad // ì í
        || 0xc3b2 <= letter && letter <= 0xc3b5 // ò ó ô õ
        || 0xc3b9 <= letter && letter <= 0xc3ba // ù ú
    )
        return 1;
    return 0;
}

/*
reads letter to check if it is a consonant
returns 1 if it is a consonant
returns 0 if not
*/
int isConsonant(unsigned int letter)
{
    if (
        letter == 0xc387                    // Ç
        || letter == 0xc3a7                 // ç
        || 0x42 <= letter && letter <= 0x44 // B C D
        || 0x46 <= letter && letter <= 0x48 // F G H
        || 0x4a <= letter && letter <= 0x4e // J K L M N
        || 0x50 <= letter && letter <= 0x54 // P Q R S T
        || 0x56 <= letter && letter <= 0x5a // V W X Y Z
        || 0x62 <= letter && letter <= 0x64 // b c d
        || 0x66 <= letter && letter <= 0x68 // f g h
        || 0x6a <= letter && letter <= 0x6e // j k l m n
        || 0x70 <= letter && letter <= 0x74 // p q r s t
        || 0x76 <= letter && letter <= 0x7a // v w x y z
    )
        return 1;
    return 0;
}

/*
reads letter to check if it is a separator (separates 2 words)
returns 1 if it is a separator
returns 0 if not
*/
int isSeparator(unsigned int letter)
{
    if (
        letter == 0x20                              // space
        || letter == 0x9                            // \t
        || letter == 0xA                            // \n
        || letter == 0xD                            // \r
        || letter == 0x5b                           // [
        || letter == 0x5d                           // ]
        || letter == 0x3f                           // ?
        || letter == 0xe28093                       // –
        || letter == 0xe280a6                       // …
        || 0x21 <= letter && letter <= 0x22         // ! "
        || 0x28 <= letter && letter <= 0x29         // ( )
        || 0x2c <= letter && letter <= 0x2e         // , - .
        || 0x3a <= letter && letter <= 0x3b         // : ;
        || 0xe2809c <= letter && letter <= 0xe2809d // “ ”
    )
        return 1;
    return 0;
}

/*
read chars with letter function until \0 or EOF
return a 3 bit value
3-bit = is EOF
2-bit : ends with consonant
1-bit: begins with vowel
*/
char readWord(FILE *file)
{
    char retval = 0;
    unsigned int nextLetter = readLetter(file);
    unsigned int letter;

    while (isSeparator(nextLetter))
    {
        nextLetter = readLetter(file);
    }

    if (nextLetter == EOF)
        return 4; // 100

    if (isVowel(nextLetter))
        retval += 1; // 001

    do
    {
        letter = nextLetter;
        nextLetter = readLetter(file);
    } while (nextLetter != EOF && !isSeparator(nextLetter));

    if (isConsonant(letter))
        retval += 2; // 010

    if (nextLetter == EOF)
        retval += 4; // 100

    return retval;
}

/*
Given a file name, returns a struct containing:
total word count
words starting with vowel
words ending in consonant
*/
struct fileStats parseFile(char *fileName)
{
    struct fileStats stats;
    stats.words = 0;
    stats.startsVowel = 0;
    stats.endsConsonant = 0;
    FILE *file = fopen(fileName, "rb");
    if (file == NULL)
        return stats;
    char wordStats;
    do
    {
        wordStats = readWord(file);
        stats.words++;
        stats.startsVowel += wordStats & 1;
        stats.endsConsonant += (wordStats & 2) >> 1;
    } while (!(wordStats >> 2));
    fclose(file);
    return stats;
}

int main(int argc, char **args)
{
    printf("%-30s %15s %15s %15s\n", "File Name", "Word Count", "Starts Vowel", "Ends Consonant");
    for (int i = 1; i < argc; i++)
    {
        struct fileStats stats = parseFile(args[i]);
        printf("%-30s %15lu %15lu %15lu\n", args[i], stats.words, stats.startsVowel, stats.endsConsonant);
    }

    return 0;
}