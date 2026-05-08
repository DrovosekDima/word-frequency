#include "sha256.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <vector>
#include <map>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <queue>

// Структура для синхронизации между потоками
struct WordFrequency {
    std::map<std::string, long long> frequencies;
    std::mutex mtx;
};

// Функция для проверки, является ли символ частью слова
inline bool isWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
}

// Функция для приведения слова к нижнему регистру
std::string toLower(const std::string& word) {
    std::string result = word;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

// Проверка, заканчивается ли SHA-256 хэш символом от 0 до 7
bool isValidHash(const std::string& hashStr) {
    if (hashStr.empty()) return false;
    char lastChar = hashStr.back();
    return (lastChar >= '0' && lastChar <= '7');
}

// Функция обработки блока текста одним потоком
void processBlock(const std::string& text, size_t startPos, size_t endPos,
                  WordFrequency& result) {
    // Извлечение слов из блока
    std::vector<std::string> words;
    std::string currentWord;
    
    for (size_t i = startPos; i < endPos && i < text.length(); i++) {
        char c = text[i];
        
        if (isWordChar(c)) {
            currentWord += c;
        } else {
            if (!currentWord.empty()) {
                words.push_back(toLower(currentWord));
                currentWord.clear();
            }
        }
    }
    if (!currentWord.empty()) {
        words.push_back(toLower(currentWord));
    }
    
    // Обработка слов с использованием SHA-256
    std::map<std::string, long long> localFrequencies;
    
    for (size_t i = 1; i < words.size(); i++) {
        // Создаём строку из предыдущего и текущего слова
        std::string combined = words[i - 1] + words[i];
        
        // Вычисляем SHA-256 хэш
        std::string hashValue = SHA256::hash(combined);
        
        // Проверяем условие (хэш заканчивается на 0-7)
        if (isValidHash(hashValue)) {
            localFrequencies[words[i]]++;
        }
    }
    
    // Добавление локальных результатов в общий результат (потокобезопасно)
    {
        std::lock_guard<std::mutex> lock(result.mtx);
        for (const auto& pair : localFrequencies) {
            result.frequencies[pair.first] += pair.second;
        }
    }
}

// Основная функция программы
int main(int argc, char* argv[]) {
    // Проверка аргументов командной строки
    if (argc < 3) {
        std::cerr << "Использование: " << argv[0] << " <входной файл> <выходной файл> [количество потоков]" << std::endl;
        std::cerr << "Пример: " << argv[0] << " input.txt output.txt 4" << std::endl;
        return 1;
    }
    
    std::string inputFile = argv[1];
    std::string outputFile = argv[2];
    int numThreads = (argc >= 4) ? std::stoi(argv[3]) : 4;
    
    // Проверка допустимого количества потоков
    int maxThreads = std::thread::hardware_concurrency();
    if (numThreads <= 0 || numThreads > maxThreads) {
        numThreads = maxThreads;
    }
    
    std::cout << "Чтение файла: " << inputFile << std::endl;
    std::cout << "Количество потоков: " << numThreads << std::endl;
    
    // Чтение входного файла
    std::ifstream infile(inputFile);
    if (!infile.is_open()) {
        std::cerr << "Ошибка: не удалось открыть файл " << inputFile << std::endl;
        return 1;
    }
    
    std::string text((std::istreambuf_iterator<char>(infile)),
                     std::istreambuf_iterator<char>());
    infile.close();
    
    if (text.empty()) {
        std::cerr << "Ошибка: файл пуст" << std::endl;
        return 1;
    }
    
    std::cout << "Размер текста: " << text.length() << " байт" << std::endl;
    
    // Структура для хранения результатов
    WordFrequency wordFreq;
    
    // Расчёт размера блока для каждого потока
    size_t blockSize = text.length() / numThreads;
    if (blockSize == 0) blockSize = text.length();
    
    // Создание потоков
    std::vector<std::thread> threads;
    
    for (int i = 0; i < numThreads; i++) {
        size_t startPos = i * blockSize;
        size_t endPos = (i == numThreads - 1) ? text.length() : (i + 1) * blockSize;
        
        // Расширение блока к концу слова, чтобы не разбивать слова между блоками
        if (endPos < text.length()) {
            while (endPos < text.length() && isWordChar(text[endPos])) {
                endPos++;
            }
        }
        
        // Также убеждаемся, что начало блока не в середине слова (кроме первого блока)
        if (startPos > 0) {
            while (startPos > 0 && isWordChar(text[startPos - 1])) {
                startPos--;
            }
        }
        
        threads.emplace_back(processBlock, std::ref(text), startPos, endPos,
                           std::ref(wordFreq));
    }
    
    // Ожидание завершения всех потоков
    for (auto& thread : threads) {
        thread.join();
    }
    
    std::cout << "Обработано " << wordFreq.frequencies.size() << " уникальных слов" << std::endl;
    
    // Сортировка по частоте (по убыванию)
    std::vector<std::pair<std::string, long long>> sortedWords(
        wordFreq.frequencies.begin(), wordFreq.frequencies.end()
    );
    
    std::sort(sortedWords.begin(), sortedWords.end(),
              [](const auto& a, const auto& b) {
                  return a.second > b.second;
              });
    
    // Запись результатов в выходной файл
    std::ofstream outfile(outputFile);
    if (!outfile.is_open()) {
        std::cerr << "Ошибка: не удалось открыть файл " << outputFile << " для записи" << std::endl;
        return 1;
    }
    
    for (const auto& pair : sortedWords) {
        outfile << pair.first << ": " << pair.second << std::endl;
    }
    
    outfile.close();
    
    std::cout << "Результаты сохранены в файл: " << outputFile << std::endl;
    std::cout << "Завершено успешно!" << std::endl;
    
    return 0;
}
