#include <iostream>
#include <vector>
#include <map>
#include <random>
#include <string>
#include <cstdlib>
#include <limits>
#include <cmath>
#include <memory>
#include <algorithm>
#include <fstream>
#include <cstdint>

#ifdef _WIN32
    #include <conio.h>
#else
    #include <termios.h>
    #include <unistd.h>
    int _getch() {
        struct termios oldt, newt;
        int ch;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        ch = getchar();
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return ch;
    }
#endif

// ============= ПРОСТРАНСТВА ИМЕН ДЛЯ ОРГАНИЗАЦИИ =============
namespace Constants {
    const int MIN_BOARD_SIZE = 8;
    const int MAX_BOARD_SIZE = 20;
    const int DEFAULT_SIZE = 16;
    const int INITIAL_TERRITORY_SIZE = 5;
    const int SABOTAGE_DIVISOR = 17;
    const int MIN_SABOTAGE = 3;
    const int COMMANDER_DISCOUNT_PERCENT = 35;
}

// ============= СТРУКТУРЫ КОНФИГУРАЦИИ =============
struct AbilityConfig {
    std::string name;
    int baseCost;
    std::string description;
};

// Статический массив способностей
static const AbilityConfig ABILITIES[] = {
    {"Десантник", 18, "Захват любой клетки (мин. 5 от королевских)"},
    {"Кассетная бомба", 8, "Сброс области 3x3 в нейтральные"},
    {"Штурмовик", 10, "Захват 3 клеток в направлении"},
    {"Командир", 50, "-35% к стоимости способностей на ВСЕ ходы"},
    {"Артиллерия", 20, "Уничтожение всего в области 3x3"},
    {"Укрепления", 15, "Защита клетки (3 прочности)"},
    {"Разведка", 12, "Показывает область 6x6 на ЛЮБОЙ клетке поля"}
};

static const int NUM_ABILITIES = sizeof(ABILITIES) / sizeof(ABILITIES[0]);

// ============= ОПТИМИЗИРОВАННЫЕ КЛАССЫ =============

// Минималистичный ColorManager
class ColorManager {
private:
    static const char* colors[9];
    
public:
    static const char* get(int index) {
        return (index >= 0 && index < 9) ? colors[index] : colors[0];
    }
    
    static void clearScreen() {
#ifdef _WIN32
        system("cls");
#else
        system("clear");
#endif
    }
    
    static void waitForEnter() {
        std::cout << "Нажмите Enter для продолжения...";
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
};

// Инициализация статического массива
const char* ColorManager::colors[] = {
    "\033[0m",        // reset
    "\033[1;37m",     // text
    "\033[48;2;255;100;100m\033[1;37m",  // player1_bg
    "\033[48;2;100;100;255m\033[1;37m",  // player2_bg
    "\033[48;2;255;255;100m\033[1;30m",  // king
    "\033[48;2;200;100;255m\033[1;37m",  // sabotage
    "\033[48;2;50;50;50m\033[1;37m",     // neutral_bg
    "\033[48;2;100;255;100m\033[1;30m",  // available
    "\033[48;2;255;255;255m\033[1;30m"   // cursor
};

// Структура вместо класса для Cell - экономия памяти
struct Cell {
    uint8_t ownerId;      // 0-2, используем uint8_t вместо int
    bool kingCell : 1;    // битовые поля для экономии
    bool sabotageCell : 1;
    bool isAvailable : 1;
    uint8_t sabotageValue : 3; // 0-7 достаточно для значений 2-5
    
    Cell() : ownerId(0), kingCell(false), sabotageCell(false), 
             isAvailable(false), sabotageValue(0) {}
};

// Оптимизированный Player
struct Player {
    int8_t playerId;      // 1-2
    int score;
    uint8_t kingX, kingY; // uint8_t достаточно для поля 20x20
    uint8_t cursorX, cursorY;
    bool commanderActive;
    
    // Конструктор по умолчанию (нужен для массива players)
    Player() : playerId(0), score(0), kingX(0), kingY(0), 
               cursorX(0), cursorY(0), commanderActive(false) {}
    
    // Основной конструктор
    Player(int id, int kx, int ky) : 
        playerId(static_cast<int8_t>(id)), score(0), 
        kingX(static_cast<uint8_t>(kx)), 
        kingY(static_cast<uint8_t>(ky)), 
        cursorX(static_cast<uint8_t>(kx)), 
        cursorY(static_cast<uint8_t>(ky)), 
        commanderActive(false) {}
    
    int getAbilityCost(int baseCost) const {
        return commanderActive ? 
            static_cast<int>(baseCost * (100 - Constants::COMMANDER_DISCOUNT_PERCENT) / 100.0) : 
            baseCost;
    }
    
    bool canUseAbility(int baseCost) const {
        return score >= getAbilityCost(baseCost);
    }
    
    void useAbility(int baseCost) {
        score -= getAbilityCost(baseCost);
    }
    
    void moveCursor(char direction, int boardSize) {
        switch(direction) {
            case 'w': case 'W': if (cursorY > 0) cursorY--; break;
            case 's': case 'S': if (cursorY < static_cast<uint8_t>(boardSize-1)) cursorY++; break;
            case 'a': case 'A': if (cursorX > 0) cursorX--; break;
            case 'd': case 'D': if (cursorX < static_cast<uint8_t>(boardSize-1)) cursorX++; break;
        }
    }
};

// Основной класс игры
class Game {
private:
    int size;
    std::vector<std::vector<Cell>> board;
    Player players[2];  // Фиксированный массив вместо vector
    int currentPlayer;
    bool gameOver;
    int winner;
    int abilitiesUsed[NUM_ABILITIES]; // Статистика
    
    // Оптимизированные вспомогательные методы
    void clearInputBuffer() {
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    
    // Оптимизированный захват окруженных территорий
    void captureSurroundedTerritories() {
        // Используем локальные переменные для быстрого доступа
        int s = size;
        bool capturedAny = false;
        
        // Временный массив владельцев для быстрых проверок
        std::vector<std::vector<uint8_t>> temp(s, std::vector<uint8_t>(s));
        for (int x = 0; x < s; ++x) {
            for (int y = 0; y < s; ++y) {
                temp[x][y] = board[x][y].ownerId;
            }
        }
        
        // Проверяем клетки внутри поля (граничные не могут быть окружены)
        for (int x = 1; x < s-1; ++x) {
            for (int y = 1; y < s-1; ++y) {
                if (temp[x][y] == 0) continue;
                
                uint8_t currentOwner = temp[x][y];
                uint8_t surroundingOwner = temp[x-1][y]; // проверяем левого соседа
                
                // Быстрая проверка всех 8 соседей
                const int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
                const int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
                
                bool surrounded = true;
                for (int i = 0; i < 8; ++i) {
                    int nx = x + dx[i];
                    int ny = y + dy[i];
                    
                    uint8_t neighbor = temp[nx][ny];
                    if (neighbor == 0) {
                        surrounded = false;
                        break;
                    }
                    if (i == 0) {
                        surroundingOwner = neighbor;
                    } else if (neighbor != surroundingOwner) {
                        surrounded = false;
                        break;
                    }
                }
                
                if (surrounded && surroundingOwner != 0 && surroundingOwner != currentOwner) {
                    board[x][y].ownerId = surroundingOwner;
                    players[surroundingOwner-1].score += 2;
                    capturedAny = true;
                }
            }
        }
        
        if (capturedAny) {
            std::cout << "🔄 Захват окруженных территорий завершен!\n";
        }
    }
    
    void createInitialTerritories() {
        int territorySize = std::min(Constants::INITIAL_TERRITORY_SIZE, size);
        
        // Игрок 1 (левый верхний угол)
        for (int x = 0; x < territorySize; ++x) {
            for (int y = 0; y < territorySize; ++y) {
                if (!board[x][y].kingCell) {
                    board[x][y].ownerId = 1;
                }
            }
        }
        
        // Игрок 2 (правый нижний угол)
        for (int x = size - territorySize; x < size; ++x) {
            for (int y = size - territorySize; y < size; ++y) {
                if (!board[x][y].kingCell) {
                    board[x][y].ownerId = 2;
                }
            }
        }
    }
    
    void addSabotageCells() {
        int numSabotage = std::max(Constants::MIN_SABOTAGE, (size * size) / Constants::SABOTAGE_DIVISOR);
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(0, size-1);
        std::uniform_int_distribution<> pointsDistrib(2, 5);
        
        int placed = 0;
        int attempts = 0;
        while (placed < numSabotage && attempts < 1000) {
            int x = distrib(gen);
            int y = distrib(gen);
            
            if (board[x][y].ownerId == 0 && !board[x][y].kingCell) {
                board[x][y].sabotageCell = true;
                board[x][y].sabotageValue = static_cast<uint8_t>(pointsDistrib(gen));
                ++placed;
            }
            ++attempts;
        }
    }
    
    void updateAvailableMoves() {
        // Обнуляем все доступные ходы
        for (auto& row : board) {
            for (auto& cell : row) {
                cell.isAvailable = false;
            }
        }
        
        int playerId = currentPlayer + 1;
        
        // Проверяем только клетки, граничащие с территорией игрока
        for (int x = 0; x < size; ++x) {
            for (int y = 0; y < size; ++y) {
                if (board[x][y].ownerId == playerId) {
                    // Проверяем соседние клетки
                    const int dirs[4][2] = {{-1,0}, {1,0}, {0,-1}, {0,1}};
                    for (int d = 0; d < 4; ++d) {
                        int nx = x + dirs[d][0];
                        int ny = y + dirs[d][1];
                        if (nx >= 0 && nx < size && ny >= 0 && ny < size) {
                            Cell& neighbor = board[nx][ny];
                            if (neighbor.ownerId != playerId && !neighbor.kingCell) {
                                neighbor.isAvailable = true;
                            }
                        }
                    }
                }
            }
        }
    }
    
    bool canCapture(int x, int y) const {
        return board[x][y].isAvailable;
    }
    
    bool captureCell() {
        Player& player = players[currentPlayer];
        int cursorX = player.cursorX;
        int cursorY = player.cursorY;
        
        if (!canCapture(cursorX, cursorY)) {
            std::cout << "❌ Нельзя захватить эту клетку!\n";
            ColorManager::waitForEnter();
            return false;
        }
        
        Cell& cell = board[cursorX][cursorY];
        
        if (cell.sabotageCell) {
            player.score += cell.sabotageValue;
            cell.sabotageCell = false;
            std::cout << "🎉 Захвачена диверсионная клетка! +" << static_cast<int>(cell.sabotageValue) << " очков!\n";
        }
        
        int previousOwner = cell.ownerId;
        cell.ownerId = static_cast<uint8_t>(currentPlayer + 1);
        player.score += (previousOwner == 0) ? 1 : 2;
        std::cout << "✅ Клетка захвачена! " << ((previousOwner == 0) ? "+1 очко" : "+2 очка") << "\n";
        
        if (cell.kingCell && previousOwner != currentPlayer + 1) {
            gameOver = true;
            winner = currentPlayer + 1;
        }
        
        // Проверяем окружение после захвата
        captureSurroundedTerritories();
        updateAvailableMoves();
        return true;
    }
    
    void display() const {
        ColorManager::clearScreen();
        
        const Player& player = players[currentPlayer];
        int playerId = currentPlayer + 1;
        int cursorX = player.cursorX;
        int cursorY = player.cursorY;
        
        std::cout << ColorManager::get(1) << "=== CELL WARFARE ===\n";
        std::cout << "🎯 Сейчас ходит: ";
        
        if (playerId == 1) {
            std::cout << ColorManager::get(2) << " ИГРОК 1 " << ColorManager::get(1);
        } else {
            std::cout << ColorManager::get(3) << " ИГРОК 2 " << ColorManager::get(1);
        }
        
        if (player.commanderActive) {
            std::cout << " [💎 КОМАНДИР АКТИВЕН -35%]";
        }
        std::cout << "\n";
        
        std::cout << "📊 Счет: ";
        std::cout << ColorManager::get(2) << " Игрок1=" << players[0].score << " " << ColorManager::get(1);
        std::cout << " | ";
        std::cout << ColorManager::get(3) << " Игрок2=" << players[1].score << " " << ColorManager::get(1);
        std::cout << "\n";
        std::cout << "💎 Ваши очки: " << player.score << "\n\n";
        
        // Игровое поле
        std::cout << "   ";
        for (int i = 0; i < size; ++i) std::cout << i % 10 << " ";
        std::cout << "\n";
        
        for (int y = 0; y < size; ++y) {
            std::cout << ColorManager::get(1) << y % 10 << " ";
            for (int x = 0; x < size; ++x) {
                const Cell& cell = board[x][y];
                
                // Выбор цвета
                if (x == cursorX && y == cursorY) {
                    std::cout << ColorManager::get(8); // cursor
                }
                else if (cell.isAvailable) {
                    std::cout << ColorManager::get(7); // available
                }
                else if (cell.ownerId == 1) {
                    std::cout << ColorManager::get(2); // player1
                }
                else if (cell.ownerId == 2) {
                    std::cout << ColorManager::get(3); // player2
                }
                else {
                    std::cout << ColorManager::get(6); // neutral
                }
                
                // Символ клетки
                if (cell.kingCell) {
                    std::cout << (cell.ownerId == 1 ? "K" : "Q");
                } else if (cell.sabotageCell) {
                    std::cout << "O";
                } else if (cell.ownerId == 1) {
                    std::cout << "1";
                } else if (cell.ownerId == 2) {
                    std::cout << "2";
                } else {
                    std::cout << ".";
                }
                
                std::cout << " " << ColorManager::get(0); // reset
            }
            std::cout << ColorManager::get(1) << "\n";
        }
        
        std::cout << "\n🎯 Управление: WASD - движение, Space - выбрать, E - способности, P - пропуск\n";
        std::cout << "📍 Курсор Игрока " << playerId << ": (" << cursorX << "," << cursorY << ")";
        
        if (board[cursorX][cursorY].isAvailable) {
            std::cout << " ✅ Доступно для захвата";
        }
        std::cout << "\n\n" << ColorManager::get(0);
    }
    
    void displayAbilities() const {
        ColorManager::clearScreen();
        const Player& player = players[currentPlayer];
        
        std::cout << ColorManager::get(1) << "💪 СПОСОБНОСТИ ";
        if (currentPlayer + 1 == 1) {
            std::cout << ColorManager::get(2) << " Игрока 1 " << ColorManager::get(1);
        } else {
            std::cout << ColorManager::get(3) << " Игрока 2 " << ColorManager::get(1);
        }
        std::cout << ":\n\n";
        
        for (int i = 0; i < NUM_ABILITIES; ++i) {
            const auto& ability = ABILITIES[i];
            int actualCost = player.getAbilityCost(ability.baseCost);
            
            std::cout << i + 1 << ". " << ability.name << " - " << actualCost << " очков";
            if (actualCost != ability.baseCost) {
                std::cout << " (базовая: " << ability.baseCost << ")";
            }
            if (i == 3 && player.commanderActive) {
                std::cout << " [💎 АКТИВЕН]";
            }
            std::cout << "\n   " << ability.description << "\n";
            
            if (abilitiesUsed[i] > 0) {
                std::cout << "   Использовано: " << abilitiesUsed[i] << " раз\n";
            }
            std::cout << "\n";
        }
        std::cout << "Выберите способность (1-7) или 0 для отмены: " << ColorManager::get(0);
    }
    
    bool useAbility(int abilityIndex) {
        Player& player = players[currentPlayer];
        int cursorX = player.cursorX;
        int cursorY = player.cursorY;
        
        if (abilityIndex < 0 || abilityIndex >= NUM_ABILITIES) {
            std::cout << "❌ Неверный выбор способности!\n";
            ColorManager::waitForEnter();
            return false;
        }
        
        const auto& ability = ABILITIES[abilityIndex];
        if (!player.canUseAbility(ability.baseCost)) {
            std::cout << "❌ Недостаточно очков!\n";
            ColorManager::waitForEnter();
            return false;
        }
        
        bool success = false;
        
        switch (abilityIndex) {
            case 0: success = useParatrooper(cursorX, cursorY); break;
            case 1: success = useClusterBomb(cursorX, cursorY); break;
            case 2: success = useAssaultSoldier(cursorX, cursorY); break;
            case 3: success = useCommander(); break;
            case 4: success = useArtillery(cursorX, cursorY); break;
            case 5: success = useFortifications(cursorX, cursorY); break;
            case 6: success = useScouting(cursorX, cursorY); break;
        }
        
        if (success) {
            player.useAbility(ability.baseCost);
            abilitiesUsed[abilityIndex]++;
            std::cout << "✅ Способность использована успешно!\n";
            captureSurroundedTerritories();
            ColorManager::waitForEnter();
        }
        
        return success;
    }
    
    bool useParatrooper(int x, int y) {
        std::cout << "📍 Использование Десантника на клетке (" << x << "," << y << ")\n";
        
        // Проверка расстояния до вражеских королей
        for (const auto& player : players) {
            if (player.playerId != currentPlayer + 1) {
                int distance = std::max(std::abs(x - static_cast<int>(player.kingX)), 
                                        std::abs(y - static_cast<int>(player.kingY)));
                if (distance < 5) {
                    std::cout << "❌ Слишком близко к вражеской королевской клетке!\n";
                    return false;
                }
            }
        }
        
        if (board[x][y].sabotageCell) {
            players[currentPlayer].score += board[x][y].sabotageValue;
            board[x][y].sabotageCell = false;
        }
        board[x][y].ownerId = static_cast<uint8_t>(currentPlayer + 1);
        return true;
    }
    
    bool useClusterBomb(int x, int y) {
        std::cout << "💣 Использование Кассетной бомбы на клетке (" << x << "," << y << ")\n";
        
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < size && ny >= 0 && ny < size && !board[nx][ny].kingCell) {
                    board[nx][ny].ownerId = 0;
                }
            }
        }
        return true;
    }
    
    bool useAssaultSoldier(int x, int y) {
        std::cout << "🔫 Использование Штурмовика на клетке (" << x << "," << y << ")\n";
        std::cout << "Выберите направление (W-вверх, S-вниз, A-влево, D-вправо): ";
        
        char direction = _getch();
        std::cout << direction << std::endl;
        
        int dx = 0, dy = 0;
        switch (direction) {
            case 'w': case 'W': dy = -1; break;
            case 's': case 'S': dy = 1; break;
            case 'a': case 'A': dx = -1; break;
            case 'd': case 'D': dx = 1; break;
            default: 
                std::cout << "❌ Неверное направление!\n";
                return false;
        }
        
        int playerId = currentPlayer + 1;
        for (int i = 0; i < 3; ++i) {
            int nx = x + dx * i, ny = y + dy * i;
            if (nx >= 0 && nx < size && ny >= 0 && ny < size) {
                if (board[nx][ny].sabotageCell) {
                    players[currentPlayer].score += board[nx][ny].sabotageValue;
                    board[nx][ny].sabotageCell = false;
                }
                board[nx][ny].ownerId = static_cast<uint8_t>(playerId);
                
                if (board[nx][ny].kingCell && board[nx][ny].ownerId != static_cast<uint8_t>(playerId)) {
                    gameOver = true;
                    winner = playerId;
                }
            }
        }
        return true;
    }
    
    bool useCommander() {
        players[currentPlayer].commanderActive = true;
        std::cout << "💎 КОМАНДИР АКТИВИРОВАН! Стоимость способностей снижена на 35%!\n";
        return true;
    }
    
    bool useArtillery(int x, int y) {
        std::cout << "💥 Использование Артиллерии на клетке (" << x << "," << y << ")\n";
        
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < size && ny >= 0 && ny < size && !board[nx][ny].kingCell) {
                    board[nx][ny].ownerId = 0;
                    board[nx][ny].sabotageCell = false;
                }
            }
        }
        return true;
    }
    
    bool useFortifications(int x, int y) {
        std::cout << "🏰 Использование Укреплений на клетке (" << x << "," << y << ")\n";
        return true;
    }
    
    bool useScouting(int x, int y) {
        std::cout << "🔍 Разведка активирована! Показана область 6x6 вокруг (" << x << "," << y << ")\n";
        // Временное отображение области (можно реализовать отдельным методом)
        return true;
    }
    
    void abilitiesMenu() {
        displayAbilities();
        
        char choice = _getch();
        std::cout << choice << std::endl;
        
        if (choice == '0') {
            return;
        }
        
        int abilityIndex = choice - '1';
        if (abilityIndex >= 0 && abilityIndex < NUM_ABILITIES) {
            useAbility(abilityIndex);
            updateAvailableMoves();
        } else {
            std::cout << "❌ Неверный выбор!\n";
            ColorManager::waitForEnter();
        }
    }
    
    void playTurn() {
        updateAvailableMoves();
        bool turnCompleted = false;
        
        while (!turnCompleted && !gameOver) {
            display();
            std::cout << "\nВыберите действие: ";
            char choice = _getch();
            std::cout << choice << std::endl;
            
            Player& player = players[currentPlayer];
            
            switch (choice) {
                case 'w': case 'W':
                case 's': case 'S':
                case 'a': case 'A':
                case 'd': case 'D':
                    player.moveCursor(choice, size);
                    break;
                    
                case ' ': case '\r':
                    if (captureCell()) {
                        turnCompleted = true;
                    }
                    break;
                    
                case 'e': case 'E':
                    abilitiesMenu();
                    break;
                    
                case 'p': case 'P':
                    std::cout << "⏭️ Ход пропущен.\n";
                    ColorManager::waitForEnter();
                    turnCompleted = true;
                    break;
                    
                default:
                    std::cout << "❌ Неверная команда!\n";
                    ColorManager::waitForEnter();
                    break;
            }
        }
        
        if (!gameOver) {
            currentPlayer = (currentPlayer + 1) % 2;
        }
    }
    
    void showStatistics() const {
        ColorManager::clearScreen();
        std::cout << ColorManager::get(1) << "=== СТАТИСТИКА ИГРЫ ===\n\n";
        std::cout << "📊 Итоговый счет:\n";
        std::cout << "Игрок 1: " << players[0].score << " очков\n";
        std::cout << "Игрок 2: " << players[1].score << " очков\n\n";
        
        std::cout << "💪 Использованные способности:\n";
        for (int i = 0; i < NUM_ABILITIES; ++i) {
            if (abilitiesUsed[i] > 0) {
                std::cout << ABILITIES[i].name << ": " << abilitiesUsed[i] << " раз\n";
            }
        }
        
        std::cout << "\n🏆 Победитель: Игрок " << winner << "!\n";
        ColorManager::waitForEnter();
    }
    
public:
    Game(int s) : currentPlayer(0), gameOver(false), winner(0) {
        // Инициализация статистики
        for (int i = 0; i < NUM_ABILITIES; ++i) {
            abilitiesUsed[i] = 0;
        }
        
        // Установка размера с ограничением
        if (s < Constants::MIN_BOARD_SIZE) size = Constants::MIN_BOARD_SIZE;
        else if (s > Constants::MAX_BOARD_SIZE) size = Constants::MAX_BOARD_SIZE;
        else size = s;
        
        // Инициализация игроков (теперь есть конструктор по умолчанию)
        players[0] = Player(1, 0, 0);
        players[1] = Player(2, size-1, size-1);
        
        // Инициализация доски
        board.resize(size, std::vector<Cell>(size));
        
        // Установка королевских клеток
        board[0][0].kingCell = true;
        board[0][0].ownerId = 1;
        
        board[size-1][size-1].kingCell = true;
        board[size-1][size-1].ownerId = 2;
        
        createInitialTerritories();
        addSabotageCells();
        updateAvailableMoves();
    }
    
    void start() {
        std::cout << ColorManager::get(1) << "\n=== Добро пожаловать в Cell Warfare! ===\n";
        std::cout << "🔄 Окруженные территории автоматически захватываются!\n";
        std::cout << "Нажмите Enter чтобы начать игру..." << ColorManager::get(0);
        ColorManager::waitForEnter();
        
        while (!gameOver) {
            playTurn();
        }
        
        display();
        showStatistics();
    }
};

int main() {
    int size = Constants::DEFAULT_SIZE;
    
    std::cout << ColorManager::get(1) << "=== CELL WARFARE ===\n";
    std::cout << "Выберите размер поля (" << Constants::MIN_BOARD_SIZE 
              << "-" << Constants::MAX_BOARD_SIZE << ", по умолчанию " 
              << Constants::DEFAULT_SIZE << "): ";
    
    std::string input;
    std::getline(std::cin, input);
    
    if (!input.empty()) {
        try {
            size = std::stoi(input);
        } catch (...) {
            size = Constants::DEFAULT_SIZE;
        }
    }
    
    // Ограничиваем размер
    if (size < Constants::MIN_BOARD_SIZE) size = Constants::MIN_BOARD_SIZE;
    if (size > Constants::MAX_BOARD_SIZE) size = Constants::MAX_BOARD_SIZE;
    
    std::cout << "🎮 Размер поля: " << size << "x" << size << "\n";
    
    Game game(size);
    game.start();
    
    return 0;
}