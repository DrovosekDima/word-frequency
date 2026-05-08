/*
Условный подсчёт частоты слов с использованием SHA-256

Задача:
Написать программу, которая считает частоту слов в текстовом файле, 
но учитывает только те слова, для которых SHA-256 хэш строки 
(предыдущее слово + текущее слово) заканчивается на символ от 0 до 7 
в шестнадцатеричном представлении.

Условия:

1. Первое слово в тексте:
   * Пропустить, так как для него нет предыдущего слова.
2. Подсчёт частоты:
   * Если условие выполняется, увеличить счётчик соответствующего слова.
3. Формат результата:
   * Вывести в файл список слов с их частотой, отсортированный по убыванию частоты.

Требования:
1. Многопоточность:
   * Распараллелить обработку текста: текст делится на блоки, 
     и каждый блок обрабатывается в отдельном потоке.
2. SHA-256:
   * Использовать простую реализацию SHA-256, которая легко интегрируется в проект.
3. Выходные данные:
   * Сохранить результаты в файл в формате: <слово>: <частота>.

Предположения:
 - Размер файла? Допустим, что помещается в память.
 - Может ли слово содержать: цифры, ', _, -. Предполагаем, что нет.
 - Слова могут быть в верхнем и нижнем регистре. Слова отличающиеся регистром считаем разными.
 - количество потоков. Предполагаем: мин = 1, макс = 64.
 - SHA-256 - https://create.stephan-brumme.com/hash-library/
             https://github.com/stbrumme/hash-library/blob/master/readme.md
*/

#include "sha256.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <string>

std::mutex outMutex; //for debugging
const size_t MIN_WORDS_PER_THREAD =  2;
const size_t MAX_NUM_THREADS      = 64; 
const size_t MIN_NUM_THREADS      =  1;
const size_t DEF_NUM_THREADS      =  4;

struct WordFrequency 
{
    std::unordered_map<std::string, size_t> frequencies;
    std::mutex mtx;
};

/*-----------------------------------------------------------------------------
  This structure is passed to each thread so that the thread knows which portion
  of the vector to process.
-------------------------------------------------------------------------------*/
struct ThreadBlock 
{
    size_t startIdx;
    size_t endIdx;
};


//std::string toLower(const std::string& word) 
//{
//    std::string result = word;
//    std::transform(result.begin(), result.end(), result.begin(),
//        [](unsigned char c) { return std::tolower(c); });
//    return result;
//}

//SHA-256 hash last char is in range 0 - 7
bool isValidHash(const std::string& hashStr) 
{
    if (hashStr.empty()) 
        return false;
    char lastChar = hashStr.back();
    return (lastChar >= '0' && lastChar <= '7');
}

/*----------------------------------------------------------------------------
  Method: readFileToWords() 
  reads a text file and extracts individual words 
  from it, returning them as a std::vector<std::string>. It processes the file
  line by line, splits each line into whitespace-separated tokens, and strips 
  trailing non-alphanumeric characters (e.g., punctuation) from each word. 
  The method throws a std::runtime_error if the file cannot be opened, 
  memory allocation fails, or any other I/O error occurs. 
  
  Note: Case conversion is currently commented out. That is "Dream" and "dream"
  are different words.
-----------------------------------------------------------------------------*/
std::vector<std::string> readFileToWords(const std::string& filename)
{
    std::vector<std::string> words;

    try
    {
        std::ifstream file(filename);

        if (!file.is_open())
        {
            throw std::runtime_error("Cannot open file: " + filename);
        }

        std::string line;
        while (std::getline(file, line))
        {
            std::istringstream iss(line);
            std::string word;

            while (iss >> word)
            {
                size_t end = word.length();

                while (end > 0 && !std::isalnum(static_cast<unsigned char>(word[end - 1])))
                {
                    end--;
                }

                if (end > 0)
                {
                    //for case insensitive: convert to lowercase!!!
                    /*for (size_t i = 0; i < end; ++i) {
                        word[i] = std::tolower(static_cast<unsigned char>(word[i]));
                    }*/

                    words.push_back(word.substr(0, end));
                }
            }
        }
    }
    catch (const std::bad_alloc& e) {
        throw std::runtime_error("Memory allocation failed: " + std::string(e.what()));
    }
    catch (const std::exception& e) {
        throw std::runtime_error("Error reading file: " + std::string(e.what()));
    }

    return words;
}

/*-----------------------------------------------------------------------------
 Method: splitWordsForThreadsBalanced() divides a sequence of words 
 (indexed 0 to totalWords-1) into balanced blocks for parallel processing. 
 It returns a vector of ThreadBlock structs, each containing a start and 
 end index (inclusive). 
 A minimum of 2 words is required for multi-threaded processing — 
 if totalWords < 2, the function forces single-threaded execution (numThreads = 1).
 It also reduces the number of threads if totalWords is too small to benefit 
 from multiple threads. The balancing is performed on word pairs 
 (totalWords - 1 pairs) rather than individual words, distributing the remainder 
 evenly among the first few threads. 
 Important: The last word of each thread block overlaps with the first word of 
 the next thread block (i.e., block[i].endIdx == block[i+1].startIdx). 
 Finally, it ensures the last block ends at the last word index.
-------------------------------------------------------------------------------*/
std::vector<ThreadBlock> splitWordsForThreadsBalanced(size_t totalWords, size_t& numThreads) 
{
    std::vector<ThreadBlock> blocks;
    if (totalWords == 0)
    {
        std::cout << "File is empty" << std::endl;
        return blocks;
    }

    if (totalWords < MIN_WORDS_PER_THREAD) 
    {
        numThreads = 1;
        blocks.push_back({ 0, totalWords - 1});
        return blocks;
    }

    if (totalWords < numThreads + 1) 
    {
        size_t maxThreads = totalWords - 1;
        if (maxThreads < 1) 
            maxThreads = 1;

        if (numThreads > maxThreads) 
        {
            std::cout << "Reducing threads from " << numThreads
                << " to " << maxThreads << " due to word count" << std::endl;
            numThreads = maxThreads;
        }
    }

    if (numThreads == 1) 
    {
        blocks.push_back({ 0, totalWords - 1});
        return blocks;
    }

    size_t totalPairs     = totalWords - 1;
    size_t pairsPerThread = totalPairs / numThreads;
    size_t remainder      = totalPairs % numThreads;

    size_t currentWordIdx = 0;

    for (size_t i = 0; i < numThreads; i++) 
    {
        ThreadBlock block;
        block.startIdx = currentWordIdx;

        size_t threadPairs = pairsPerThread + (i < remainder ? 1 : 0);

        block.endIdx = currentWordIdx + threadPairs;

        blocks.push_back(block);

        currentWordIdx = block.endIdx;
    }

    blocks.back().endIdx = totalWords - 1;

    return blocks;
}

// thread function
void processBlock(const std::vector<std::string>& words,
                        size_t                    startIdx,
                        size_t                    endIdx,
                        WordFrequency&            result) 
{

    {
        std::lock_guard<std::mutex> guardLock(outMutex);
        std::cout << "Thread(" << std::this_thread::get_id() << ") Processing block ["
            << startIdx << ".." << endIdx << "]" << std::endl;
    }

    SHA256 sha256;
    std::unordered_map<std::string, size_t> localFreq;

    /*----------------------------------------------------------
     Note that we skip the first word because there is no previous;
    ------------------------------------------------------------*/
    for (size_t i = startIdx + 1; i <= endIdx; ++i) 
    {
        std::string combined = words[i - 1] + words[i];
        std::string hashValue = sha256(combined);

        if (isValidHash(hashValue))
        {
            {
                std::lock_guard<std::mutex> guardLock(outMutex);
                std::cout << "Thread(" << std::this_thread::get_id() << ")" << "Added:\t'"
                    << words[i] << "' prev is '" << words[i - 1] << "'\t hash is " << hashValue << std::endl;
            }
            localFreq[words[i]]++;
        }
        else
        {
            std::lock_guard<std::mutex> guardLock(outMutex);
            std::cout << "Thread(" << std::this_thread::get_id() << ")" << "Skipped:\t'"
                << words[i] << "' prev is '" << words[i - 1] << "'\t hash is " << hashValue << std::endl;
        }
    }

    /*----------------------------------------------------------
      Copy local data (localFreq) to the global (result);
    ------------------------------------------------------------*/
    {
        std::lock_guard<std::mutex> lock(result.mtx);
        for (const auto& [word, count] : localFreq) 
        {
            result.frequencies[word] += count;
        }
    }
}

/*-----------------------------------------------------------------------------
  writeResultsToFile writes a list of word-frequency pairs to a text file. 
  It takes an output filename and a sorted vector of pairs (word, count), 
  then writes each word followed by its count in the format "word: count" 
  on separate lines.
------------------------------------------------------------------------------*/
void writeResultsToFile(const std::string& outputFile, 
                        const std::vector<std::pair<std::string, size_t>>& sortedWords)
{
    try 
    {
        std::ofstream outfile(outputFile);
        if (!outfile.is_open())
        {
            throw std::runtime_error("Failed to create file " + outputFile + " for writing");
        }

        for (const auto& pair : sortedWords)
        {
            outfile << pair.first << ": " << pair.second << std::endl;
        }

        std::cout << "Result saved to file: " << outputFile << std::endl;
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        throw;
    }
    catch (const std::bad_alloc& e)
    {
        std::cerr << "Memory allocation error: " << e.what() << std::endl;
        throw;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Unexpected error: " << e.what() << std::endl;
        throw;
    }
}

int main(int argc, char* argv[]) 
{

    if (argc < 3) 
    {
        std::cout << "Usage: " << argv[0] << " <input file> <output file> [number of threads]" << std::endl;
        std::cout << "Min value of number threads is 1, max is 64" << std::endl;
        std::cout << "For example: " << argv[0] << " input.txt output.txt 5" << std::endl;
        return 1;
    }
    try
    {
        std::string              inputFile = argv[1];
        std::string              outputFile = argv[2];
        size_t                   numThreads;
        std::vector<std::string> words;
        WordFrequency            wordFreq;
        std::vector<std::thread> threads;
    
        /*-------------------------------------------------------------------------
          1. Validating input parameters.   
        -------------------------------------------------------------------------*/
        if (argc >= 4)
        {
           numThreads = std::stoi(argv[3]);
           if (numThreads > MAX_NUM_THREADS || numThreads < MIN_NUM_THREADS)
           {
               std::cout << "Number of threads should be in [1-64]";
               return -1;
           }
        }
        else
        {
            numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0)
            {
               numThreads = DEF_NUM_THREADS;
            }
        }

        std::cout << "Reading file: " << inputFile << std::endl;
        /*-------------------------------------------------------------------------
          2. Read entire file to vector of strings
        -------------------------------------------------------------------------*/
        words = readFileToWords(inputFile);

        std::cout << "Number of words: " << words.size() << std::endl;
        /*-------------------------------------------------------------------------
         3. Divide words between threads. Min number of words for one thread is 2.
            Note
             - blocks contains [startIndex, endIndex]
             - endIndex of the first block = startIndex of the second block.
        ---------------------------------------------------------------------------*/
        auto blocks = splitWordsForThreadsBalanced(words.size(), numThreads);

        for (const auto& block : blocks)
        {
            threads.emplace_back(processBlock,
                std::ref(words),
                block.startIdx,
                block.endIdx,
                std::ref(wordFreq));
        }


        for (auto& thread : threads)
        {
            thread.join();
        }

        std::cout << "Num words to write to file is " << wordFreq.frequencies.size() << std::endl;

        /*------------------------------------------------------------------------
        4. Sort by frequency
        --------------------------------------------------------------------------*/
        std::vector<std::pair<std::string, size_t>> sortedWords(wordFreq.frequencies.begin(), wordFreq.frequencies.end());

        std::sort(sortedWords.begin(), sortedWords.end(),
            [](const auto& a, const auto& b) 
            {
                if (a.second != b.second) 
                   return a.second > b.second;
                else
                   return a.first < b.first;
            });

        /*------------------------------------------------------------------------
         5.  Write to file
        --------------------------------------------------------------------------*/
        writeResultsToFile(outputFile, sortedWords);
        std::cout << "Succeeded!" << std::endl;
    }
    catch (const std::invalid_argument& e) 
    {
        std::cout << "Panic. Invalid argum: " << e.what() << std::endl;
        return 1;
    }
    catch (const std::runtime_error& e)
    {
        std::cout << "Panic. Exception: " << e.what() << std::endl;
        return 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Panic. Fatal error: " << e.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cout << "Panic. Something went wrong ... " << std::endl;
        return 1;
    }

    return 0;
}
