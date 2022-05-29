/**
 * @file dispatcher.c (implementation file)
 *
 * @brief Problem name: multiprocess word count
 *
 * Contains implementation of the dispatcher process.
 *
 * @author Pedro Casimiro, nmec: 93179
 * @author Diogo Bento, nmec: 93391
 */

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>

#include "dispatcher.h"
#include "utfUtils.h"
#include "sharedRegion.h"

/**
 * @brief Reads a chunk from a file stream into a byte array.
 *
 * @param file file stream to be read
 * @return Task struct with the number of bytes read and byte array
 */

/** @brief The initial max number of bytes of the text chunk in a task. */
static const int MAX_BYTES_READ = 500;

/** @brief How many bytes the first text read leaves empty. */
static const int BYTES_READ_BUFFER = 50;


/**
 * @brief Reads an UTF-8 character from a file stream.
 *
 * @param file stream of the file to be read
 * @return UTF-8 character
 */
int readLetterFromFile(FILE *file)
{
    int letter = 0;
    if (fread(&letter, 1, 1, file) != 1)
        return EOF;

    // how many extra bytes need to be read after the first to get a full character
    // if -1 initial byte is invalid
    int loops = -1 + byte0utf8(letter) + 2 * byte1utf8(letter) + 3 * byte2utf8(letter) + 4 * byte3utf8(letter);
    if (loops < 0)
    {
        errno = EINVAL;
        perror("Invalid text found");
        return EOF;
    }

    for (int i = 0; i < loops; i++)
    {
        letter <<= 8;
        fread(&letter, 1, 1, file);
    }

    return letter;
}

static Task readBytes(FILE *file)
{
    Task task = {.byteCount = -1,
                 .bytes = malloc(sizeof(char) * MAX_BYTES_READ)};
    task.byteCount = fread(task.bytes, 1, (MAX_BYTES_READ - BYTES_READ_BUFFER), file);

    // if initial fread didn't read expected number of bytes it means it reached EOF
    if (task.byteCount != MAX_BYTES_READ - BYTES_READ_BUFFER)
    {
        task.bytes[task.byteCount] = EOF;
        return task;
    }

    // if initial fread ended in the middle of a character, add enough bytes to
    // make sure task.bytes ends at the end of a character
    while (true)
    {
        // how many extra bytes need to be read after the first to get a full character
        // if -1 byte is in the middle of a character
        int loops = -1 + byte0utf8(task.bytes[task.byteCount - 1]) + 2 * byte1utf8(task.bytes[task.byteCount - 1]) + 3 * byte2utf8(task.bytes[task.byteCount - 1]) + 4 * byte3utf8(task.bytes[task.byteCount - 1]);
        if (loops >= 0)
            task.byteCount += fread(task.bytes + task.byteCount, 1, loops, file);
        else
        {
            // as current byte is in the middle of a character read another one
            // and check again if its not in the middle

            // if EOF, should never happen in a valid UTF-8 file
            if (fread(task.bytes + task.byteCount, 1, 1, file) != 1)
                break;
            task.byteCount++;
            continue;
        }
        break;
    }

    int letter;

    // used to realloc byte array if needed
    int localMaxBytes = MAX_BYTES_READ;

    // read characters until the byte array doesn't end in the middle of a word
    do
    {
        // if byte array is almost full, increase its size
        if (task.byteCount >= localMaxBytes - 10)
        {
            localMaxBytes += 100;
            task.bytes = (char *)realloc(task.bytes, localMaxBytes);
        }

        letter = readLetterFromFile(file);

        // how many bytes this letter uses
        int loops = ((letter & 0xff000000) != 0) + ((letter & 0xffff0000) != 0) + ((letter & 0xffffff00) != 0);

        // store character bytes into byte array
        for (int i = loops; i >= 0; i--)
        {
            task.bytes[task.byteCount++] = letter >> (8 * i);
        }
    } while (!(isSeparator(letter) || isBridge(letter) || letter == EOF));

    task.bytes[task.byteCount] = EOF;

    return task;
}



void *dispatchFileTasksIntoSender(){
    int nextDispatch = 1;
    for (int fIdx=0;fIdx<totalFileCount;fIdx++){
        char * filename = files[fIdx];
        FILE *file = fopen(filename, "rb");
        initResult();
        if (file == NULL){
            // file is a dud
            continue;
        }


        Task task;
        
        while(true){
            //get chunk
            task = readBytes(file);
            //exit if no chunk
            if (task.byteCount == 0) {
                free(task.bytes);
                break;
            }
            incrementChunks(fIdx);

            //send task into respective queue, this may block
            pushTaskToSender(nextDispatch,task);

            //advance dispatch number, wraps back to 1 after size
            nextDispatch++;
            if (nextDispatch>=groupSize)
                nextDispatch=1;
        }
    }
    finishedReading();

    //send signal to stop workers
    Task stop = {.byteCount = -1};
    for (int i = 1; i < groupSize; i++)
        pushTaskToSender(i,stop);
        // signal that there's nothing left for workers to process
        // MPI_Send(&stop, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
    
    pthread_exit((int *)EXIT_SUCCESS);
}

/**
 * @brief Emits chunks toward workers in a non-blocking manner
 * 
 * @return void* 
 */
void* emitTasksToWorkers(){
    //curently employed entities
    bool working[groupSize-1];
    int currentlyWorking = groupSize-1;

    //request handler objects, last chunk received
    int requests[groupSize-1];
    Task tasks[groupSize-1];

    //init data for this function
    for (int i=0;i<groupSize-1;i++){
        requests[i] = MPI_REQUEST_NULL;
        tasks[i].byteCount = 0;
        working[i] = true;
    }

    //used to signal workers that no more tasks are coming
    int killSignal = 0;
    
    //only ever used to receive waitAny result
    int lastFinish;

    int testStatus;

    while (true){
        
        for (int i=0;i<groupSize-1;i++){
            
            MPI_Test(requests+i,&testStatus,MPI_STATUS_IGNORE);
            if(working[i] && testStatus){
                //worker is ready
                //clear out last task
                if (tasks[i].byteCount>0)
                    free(tasks[i].bytes);
                //try to get a new task
                if (!getTask(i,tasks+i)){
                    tasks[i].byteCount = 0; //task get failed, we use this to avoid free in future loops
                }

                else{
                    //is a kill request, signal worker to stop and mark this worker as dead
                    if (tasks[i].byteCount == -1){
                        currentlyWorking--;
                        MPI_Isend(&killSignal,1,MPI_INT,i+1,0,MPI_COMM_WORLD,requests+i);
                        working[i] =false;
                        continue;
                    }

                    //send this task to worker in a non-blocking manner
                    MPI_Isend( &tasks[i].byteCount , 1 , MPI_INT , i+1, 0 , MPI_COMM_WORLD,requests+i);
                    MPI_Request_free(requests+i);
                    MPI_Isend( tasks[i].bytes, tasks[i].byteCount , MPI_CHAR , i+1 , 0 , MPI_COMM_WORLD,requests+i);
                }

            }
           
    }

    if (currentlyWorking>0){
        //wait for any chunks to be available and for a worker to be available
        awaitFurtherInfo();
        MPI_Waitany(groupSize-1,requests,&lastFinish,MPI_STATUS_IGNORE);
    }

    else
        break;
   
    }

     //wait for all the kill messages to have been sent
    for (int i = 0; i < groupSize-1; i++)
        // signal that there's nothing left for workers to process
        MPI_Wait(&requests[i], MPI_STATUS_IGNORE);
    
    pthread_exit((int *)EXIT_SUCCESS);
}

/**
 * @brief Merges file chunks read by workers into their results structure
 * 
 * @return void* 
 */
void *mergeChunks(){
    int nextReceive = 1;
    int read; //move data here
    //for each file
    for(int i=0;i<totalFileCount;i++){
        Result* res = getResultToUpdate(i); //blocks until this results object has been initialized
        int currentChunk = 0;
        while (hasMoreChunks(i, currentChunk++)) {
            //get wc,start vowel, end consonant
            MPI_Recv(&read,1,MPI_INT,nextReceive,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
            (*res).wordCount+=read;
            MPI_Recv(&read,1,MPI_INT,nextReceive,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
            (*res).vowelStartCount+=read;
            MPI_Recv(&read,1,MPI_INT,nextReceive,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
            (*res).consonantEndCount+=read;
            //avance dispatch
            nextReceive++;
            if(nextReceive>=groupSize)
                nextReceive=1;
        }
    }
    
    pthread_exit((int *)EXIT_SUCCESS);
}