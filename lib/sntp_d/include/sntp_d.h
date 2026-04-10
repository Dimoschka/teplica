#ifndef SNTP_D_H
#define SNTP_D_H

#include <time.h>
#include <stdbool.h>

/**
 * @brief Callback функция для событий синхронизации времени
 * 
 * @param tv Указатель на структуру timeval с полученным временем
 */
typedef void (*sntp_sync_callback_t)(struct timeval *tv);

/**
 * @brief Инициализация и запуск синхронизации времени через NTP
 * 
 * Функция выполняет:
 * - Инициализацию сетевого интерфейса
 * - Остановку предыдущей синхронизации (если была)
 * - Установку серверов NTP
 * - Установку часового пояса
 * - Запуск синхронизации
 */
void sntpStart(void);

/**
 * @brief Остановка синхронизации времени
 */
void sntpStop(void);

/**
 * @brief Регистрация callback функции для событий синхронизации
 * 
 * @param callback Указатель на функцию-обработчик события синхронизации
 */
void sntpRegisterCallback(sntp_sync_callback_t callback);

/**
 * @brief Установка часового пояса
 * 
 * @param timezone Строка часового пояса (например, "MSK-5", "UTC+0", "Europe/Moscow")
 */
void sntpSetTimezone(const char *timezone);

/**
 * @brief Установка NTP серверов
 * 
 * @param server1 Первый NTP сервер (например, "pool.ntp.org")
 * @param server2 Второй NTP сервер (например, "time.nist.gov")
 */
void sntpSetServers(const char *server1, const char *server2);

/**
 * @brief Проверка, синхронизировано ли время
 * 
 * @return true если время синхронизировано, false если нет
 */
bool sntpIsTimeSet(void);

#endif /* SNTP_D_H */
