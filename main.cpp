#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <windows.h>
#include "sqlite3.h"

// Глобальные атомарные переменные для потокобезопасности
std::atomic<bool> g_clicking{ false };
std::atomic<bool> g_running{ true };
std::atomic<int> g_cps{ 10 };

// БЭКЕНД: Логика кликера (работает в отдельном потоке)
void ClickerThread() {
    while (g_running) {
        if (g_clicking) {
            INPUT inputs[2] = {};
            inputs[0].type = INPUT_MOUSE;
            inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            inputs[1].type = INPUT_MOUSE;
            inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;

            SendInput(2, inputs, sizeof(INPUT));

            int current_cps = g_cps.load();
            if (current_cps > 0) {
                Sleep(1000 / current_cps);
            }
            else {
                Sleep(50);
            }
        }
        else {
            Sleep(10); // Ждем, не нагружая процессор
        }
    }
}

// БАЗА ДАННЫХ: Инициализация и настройка SQLite
void InitDatabase(sqlite3*& db) {
    if (sqlite3_open("macros.db", &db)) {
        std::cerr << "Ошибка открытия БД: " << sqlite3_errmsg(db) << "\n";
        exit(1);
    }

    const char* sql = "CREATE TABLE IF NOT EXISTS profiles ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT UNIQUE, "
        "cps INTEGER);";
    char* errMsg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "Ошибка создания таблицы: " << errMsg << "\n";
        sqlite3_free(errMsg);
    }
}

void CreateProfile(sqlite3* db) {
    std::string name;
    int cps;
    std::cout << "Введите имя нового профиля: ";
    std::cin >> name;
    std::cout << "Введите желаемый CPS: ";
    std::cin >> cps;

    std::string sql = "INSERT INTO profiles (name, cps) VALUES ('" + name + "', " + std::to_string(cps) + ");";
    char* errMsg = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "Ошибка или профиль уже существует: " << errMsg << "\n";
        sqlite3_free(errMsg);
    }
    else {
        std::cout << "[Успешно] Профиль '" << name << "' сохранен в базу!\n";
    }
}

void SelectProfile(sqlite3* db) {
    std::string name;
    std::cout << "Введите имя профиля для загрузки: ";
    std::cin >> name;

    std::string sql = "SELECT cps FROM profiles WHERE name = '" + name + "';";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int loaded_cps = sqlite3_column_int(stmt, 0);
            g_cps.store(loaded_cps);
            std::cout << "[Успешно] Профиль загружен. Текущий CPS: " << loaded_cps << "\n";
        }
        else {
            std::cout << "[Ошибка] Профиль не найден.\n";
        }
    }
    sqlite3_finalize(stmt);
}

// ФРОНТЕНД: CLI Интерфейс пользователя
int main() {
    setlocale(LC_ALL, "Russian");
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    sqlite3* db;
    InitDatabase(db);

    // Запускаем бэкенд-поток
    std::thread clicker(ClickerThread);

    std::cout << "=== MACRO ===\n";
    std::cout << "Горячая клавиша: Средняя кнопка мыши (Start/Stop)\n";
    std::cout << "Для выхода из программы введите 0 в меню.\n";

    // Асинхронная проверка горячей клавиши
    std::thread hotkey_listener([]() {
        bool prev_mid = false;
        while (g_running) {
            bool cur_mid = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
            if (cur_mid && !prev_mid) {
                g_clicking = !g_clicking;
                if (g_clicking) {
                    std::cout << "\n[ДВИЖОК] Макрос ЗАПУЩЕН! (CPS: " << g_cps.load() << ")\n> ";
                }
                else {
                    std::cout << "\n[ДВИЖОК] Макрос ОСТАНОВЛЕН!\n> ";
                }
            }
            prev_mid = cur_mid;
            Sleep(10);
        }
        });

    // Главный цикл CLI (Frontend)
    int choice = -1;
    while (choice != 0) {
        std::cout << "\n--- МЕНЮ ---\n";
        std::cout << "1. Создать профиль\n";
        std::cout << "2. Выбрать профиль\n";
        std::cout << "3. Показать текущий CPS\n";
        std::cout << "0. Выход\n";
        std::cout << "Выбор: ";
        std::cin >> choice;

        switch (choice) {
        case 1: CreateProfile(db); break;
        case 2: SelectProfile(db); break;
        case 3: std::cout << "Текущий установленный CPS: " << g_cps.load() << "\n"; break;
        case 0: g_running = false; break;
        default: std::cout << "Неверный выбор.\n"; break;
        }
    }

    // Чистим ресурсы
    sqlite3_close(db);
    clicker.join();
    hotkey_listener.join();

    std::cout << "Программа завершена.\n";
    return 0;
}