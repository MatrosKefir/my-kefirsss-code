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
#include <queue>

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
    const int BOARD_SIZE_SMALL = 16;
    const int BOARD_SIZE_MEDIUM = 32;
    const int BOARD_SIZE_LARGE = 64;
    const int MIN_BOARD_SIZE = 16;
    const int MAX_BOARD_SIZE = 64;
    const int DEFAULT_SIZE = 16;
    const int INITIAL_TERRITORY_SIZE_SMALL = 5;
    const int INITIAL_TERRITORY_SIZE_MEDIUM = 8;
    const int INITIAL_TERRITORY_SIZE_LARGE = 12;
    const int SABOTAGE_DIVISOR_SMALL = 17;
    const int SABOTAGE_DIVISOR_MEDIUM = 25;
    const int SABOTAGE_DIVISOR_LARGE = 40;
    const int MIN_SABOTAGE_SMALL = 3;
    const int MIN_SABOTAGE_MEDIUM = 5;
    const int MIN_SABOTAGE_LARGE = 10;
    const int COMMANDER_DISCOUNT_PERCENT = 35;
    const int VISIBILITY_RADIUS_SMALL = 3;
    const int VISIBILITY_RADIUS_MEDIUM = 5;
    const int VISIBILITY_RADIUS_LARGE = 8;
    const int SCOUTING_RADIUS_SMALL = 6;
    const int SCOUTING_RADIUS_MEDIUM = 10;
    const int SCOUTING_RADIUS_LARGE = 15;
    const int FORTIFICATION_COST = 6; // Новая цена укреплений
}

// ============= СТРУКТУРЫ КОНФИГУРАЦИИ =============
struct AbilityConfig {
    std::string name;
    int baseCost;
    std::string description;
};

// Статический массив способностей с обновленной ценой укреплений
static const AbilityConfig ABILITIES[] = {
    {"Десантник", 18, "Захват любой клетки (мин. 5 от королевских)"},
    {"Кассетная бомба", 8, "Сброс области 2x2 в нейтральные"},
    {"Штурмовик", 10, "Захват 3 клеток в направлении"},
    {"Командир", 50, "-35% к стоимости способностей на ВСЕ ходы"},
    {"Артиллерия", 20, "Уничтожение всего в области 3x3"},
    {"Укрепления", 6, "Защита 2 клеток в направлении (только артиллерия)"},
    {"Разведка", 12, "Показывает область вокруг клетки"}
};

static const int NUM_ABILITIES = sizeof(ABILITIES) / sizeof(ABILITIES[0]);

// ============= ОПТИМИЗИРОВАННЫЕ КЛАССЫ =============

// Минималистичный ColorManager
class ColorManager {
private:
    static const char* colors[11]; // Добавлен цвет для укреплений
    
public:
    static const char* get(int index) {
        return (index >= 0 && index < 11) ? colors[index] : colors[0];
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

// Инициализация статического массива с новым цветом для укреплений
const char* ColorManager::colors[] = {
    "\033[0m",        // reset
    "\033[1;37m",     // text
    "\033[48;2;255;100;100m\033[1;37m",  // player1_bg
    "\033[48;2;100;100;255m\033[1;37m",  // player2_bg
    "\033[48;2;255;255;100m\033[1;30m",  // king
    "\033[48;2;200;100;255m\033[1;37m",  // sabotage
    "\033[48;2;50;50;50m\033[1;37m",     // neutral_bg
    "\033[48;2;100;255;100m\033[1;30m",  // available
    "\033[48;2;255;255;255m\033[1;30m",  // cursor
    "\033[48;2;30;30;30m\033[1;30m",     // fog of war
    "\033[48;2;139;69;19m\033[1;37m"     // fortification (коричневый)
};

// Структура для Cell с поддержкой тумана войны и укреплений
struct Cell {
    uint8_t ownerId;      // 0-2
    bool kingCell : 1;
    bool sabotageCell : 1;
    bool isAvailable : 1;
    bool isVisible : 1;   // Видима ли клетка текущему игроку
    bool isExplored : 1;  // Была ли исследована клетка
    bool isFortified : 1; // Новое: укреплена ли клетка
    uint8_t sabotageValue : 3;
    uint8_t lastSeenOwner : 2; // Кто был владельцем, когда видели в последний раз
    
    Cell() : ownerId(0), kingCell(false), sabotageCell(false), 
             isAvailable(false), isVisible(false), isExplored(false),
             isFortified(false), sabotageValue(0), lastSeenOwner(0) {}
};

// Оптимизированный Player
struct Player {
    int8_t playerId;      // 1-2
    int score;
    uint16_t kingX, kingY; // Используем uint16_t для больших полей
    uint16_t cursorX, cursorY;
    bool commanderActive;
    bool abilityUsedThisTurn;
    
    // Конструктор по умолчанию
    Player() : playerId(0), score(0), kingX(0), kingY(0), 
               cursorX(0), cursorY(0), commanderActive(false),
               abilityUsedThisTurn(false) {}
    
    // Основной конструктор
    Player(int id, int kx, int ky) : 
        playerId(static_cast<int8_t>(id)), score(0), 
        kingX(static_cast<uint16_t>(kx)), 
        kingY(static_cast<uint16_t>(ky)), 
        cursorX(static_cast<uint16_t>(kx)), 
        cursorY(static_cast<uint16_t>(ky)), 
        commanderActive(false),
        abilityUsedThisTurn(false) {}
    
    int getAbilityCost(int baseCost) const {
        return commanderActive ? 
            static_cast<int>(baseCost * (100 - Constants::COMMANDER_DISCOUNT_PERCENT) / 100.0) : 
            baseCost;
    }
    
    bool canUseAbility(int baseCost) const {
        return score >= getAbilityCost(baseCost) && !abilityUsedThisTurn;
    }
    
    void useAbility(int baseCost) {
        score -= getAbilityCost(baseCost);
        abilityUsedThisTurn = true;
    }
    
    void resetTurn() {
        abilityUsedThisTurn = false;
    }
    
    void moveCursor(char direction, int boardSize) {
        switch(direction) {
            case 'w': case 'W': if (cursorY > 0) cursorY--; break;
            case 's': case 'S': if (cursorY < static_cast<uint16_t>(boardSize-1)) cursorY++; break;
            case 'a': case 'A': if (cursorX > 0) cursorX--; break;
            case 'd': case 'D': if (cursorX < static_cast<uint16_t>(boardSize-1)) cursorX++; break;
        }
    }
};

// Основной класс игры
class Game {
private:
    int size;
    std::vector<std::vector<Cell>> board;
    Player players[2];
    int currentPlayer;
    bool gameOver;
    int winner;
    int abilitiesUsed[NUM_ABILITIES];
    int visibilityRadius;
    int scoutingRadius;
    int initialTerritorySize;
    int sabotageDivisor;
    int minSabotage;
    
    void clearInputBuffer() {
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    
    // Установка параметров в зависимости от размера поля
    void setGameParameters() {
        if (size == Constants::BOARD_SIZE_SMALL) {
            visibilityRadius = Constants::VISIBILITY_RADIUS_SMALL;
            scoutingRadius = Constants::SCOUTING_RADIUS_SMALL;
            initialTerritorySize = Constants::INITIAL_TERRITORY_SIZE_SMALL;
            sabotageDivisor = Constants::SABOTAGE_DIVISOR_SMALL;
            minSabotage = Constants::MIN_SABOTAGE_SMALL;
        } else if (size == Constants::BOARD_SIZE_MEDIUM) {
            visibilityRadius = Constants::VISIBILITY_RADIUS_MEDIUM;
            scoutingRadius = Constants::SCOUTING_RADIUS_MEDIUM;
            initialTerritorySize = Constants::INITIAL_TERRITORY_SIZE_MEDIUM;
            sabotageDivisor = Constants::SABOTAGE_DIVISOR_MEDIUM;
            minSabotage = Constants::MIN_SABOTAGE_MEDIUM;
        } else { // size == Constants::BOARD_SIZE_LARGE
            visibilityRadius = Constants::VISIBILITY_RADIUS_LARGE;
            scoutingRadius = Constants::SCOUTING_RADIUS_LARGE;
            initialTerritorySize = Constants::INITIAL_TERRITORY_SIZE_LARGE;
            sabotageDivisor = Constants::SABOTAGE_DIVISOR_LARGE;
            minSabotage = Constants::MIN_SABOTAGE_LARGE;
        }
    }
    
    // Обновление видимости клеток для текущего игрока
    void updateVisibility() {
        int playerId = currentPlayer + 1;
        
        // Сначала скрываем все клетки
        for (auto& row : board) {
            for (auto& cell : row) {
                cell.isVisible = false;
            }
        }
        
        // Показываем клетки в радиусе от территорий игрока
        for (int x = 0; x < size; ++x) {
            for (int y = 0; y < size; ++y) {
                if (board[x][y].ownerId == playerId) {
                    // Область видимости
                    for (int dx = -visibilityRadius; dx <= visibilityRadius; ++dx) {
                        for (int dy = -visibilityRadius; dy <= visibilityRadius; ++dy) {
                            int nx = x + dx;
                            int ny = y + dy;
                            
                            if (nx >= 0 && nx < size && ny >= 0 && ny < size) {
                                board[nx][ny].isVisible = true;
                                board[nx][ny].isExplored = true;
                                board[nx][ny].lastSeenOwner = board[nx][ny].ownerId;
                            }
                        }
                    }
                }
            }
        }
        
        // Всегда показываем королевские клетки текущего игрока и клетки курсора
        Player& player = players[currentPlayer];
        board[player.kingX][player.kingY].isVisible = true;
        board[player.cursorX][player.cursorY].isVisible = true;
        
        // Показываем территории противника, которые были исследованы
        int opponentId = (playerId == 1) ? 2 : 1;
        for (int x = 0; x < size; ++x) {
            for (int y = 0; y < size; ++y) {
                if (board[x][y].isExplored && (board[x][y].ownerId == opponentId || board[x][y].kingCell || board[x][y].isFortified)) {
                    board[x][y].isVisible = true;
                }
            }
        }
    }
    
    // Захват окруженных нейтральных территорий
    void captureSurroundedNeutralTerritories() {
        int s = size;
        bool capturedAny = false;
        
        std::vector<std::vector<bool>> visited(s, std::vector<bool>(s, false));
        
        for (int x = 0; x < s; ++x) {
            for (int y = 0; y < s; ++y) {
                if (!visited[x][y] && board[x][y].ownerId == 0 && !board[x][y].isFortified) {
                    // BFS для поиска области нейтральных клеток
                    std::vector<std::pair<int, int>> neutralCells;
                    std::queue<std::pair<int, int>> q;
                    q.push({x, y});
                    visited[x][y] = true;
                    
                    bool surrounded = true;
                    int surroundingOwner = 0;
                    
                    const int dx[] = {-1, 0, 1, 0};
                    const int dy[] = {0, -1, 0, 1};
                    
                    while (!q.empty()) {
                        auto current = q.front();
                        q.pop();
                        int cx = current.first;
                        int cy = current.second;
                        
                        neutralCells.push_back({cx, cy});
                        
                        // Проверяем соседей
                        for (int i = 0; i < 4; ++i) {
                            int nx = cx + dx[i];
                            int ny = cy + dy[i];
                            
                            if (nx < 0 || nx >= s || ny < 0 || ny >= s) {
                                surrounded = false;
                                continue;
                            }
                            
                            if (board[nx][ny].ownerId == 0 && !board[nx][ny].isFortified) {
                                if (!visited[nx][ny]) {
                                    visited[nx][ny] = true;
                                    q.push({nx, ny});
                                }
                            } else if (board[nx][ny].isFortified) {
                                // Укрепления прерывают захват
                                surrounded = false;
                            } else {
                                // Сосед принадлежит игроку
                                if (surroundingOwner == 0) {
                                    surroundingOwner = board[nx][ny].ownerId;
                                } else if (board[nx][ny].ownerId != surroundingOwner) {
                                    // Разные владельцы вокруг - не окружена
                                    surrounded = false;
                                }
                            }
                        }
                    }
                    
                    // Если область окружена одним игроком, захватываем ее
                    if (surrounded && surroundingOwner != 0 && !neutralCells.empty()) {
                        Player& capturingPlayer = players[surroundingOwner - 1];
                        int pointsEarned = 0;
                        
                        for (auto& cellPos : neutralCells) {
                            int cx = cellPos.first;
                            int cy = cellPos.second;
                            
                            board[cx][cy].ownerId = static_cast<uint8_t>(surroundingOwner);
                            pointsEarned += 1;
                            
                            // Если были саботажные клетки
                            if (board[cx][cy].sabotageCell) {
                                pointsEarned += board[cx][cy].sabotageValue;
                                board[cx][cy].sabotageCell = false;
                                board[cx][cy].sabotageValue = 0;
                            }
                        }
                        
                        capturingPlayer.score += pointsEarned;
                        capturedAny = true;
                        
                        std::cout << "\n🔄 Игрок " << surroundingOwner 
                                  << " захватил окруженную нейтральную область из " 
                                  << neutralCells.size() << " клеток! +" 
                                  << pointsEarned << " очков\n";
                    }
                }
            }
        }
        
        if (capturedAny) {
            ColorManager::waitForEnter();
        }
    }
    
    // Захват окруженных территорий противника (старая механика)
    void captureSurroundedTerritories() {
        int s = size;
        bool capturedAny = false;
        
        std::vector<std::vector<uint8_t>> temp(s, std::vector<uint8_t>(s));
        for (int x = 0; x < s; ++x) {
            for (int y = 0; y < s; ++y) {
                temp[x][y] = board[x][y].ownerId;
            }
        }
        
        for (int x = 1; x < s-1; ++x) {
            for (int y = 1; y < s-1; ++y) {
                if (temp[x][y] == 0 || board[x][y].isFortified) continue;
                
                uint8_t currentOwner = temp[x][y];
                uint8_t surroundingOwner = temp[x-1][y];
                
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
        int territorySize = std::min(initialTerritorySize, size);
        
        // Территория игрока 1 (левый верхний угол)
        for (int x = 0; x < territorySize; ++x) {
            for (int y = 0; y < territorySize; ++y) {
                if (!board[x][y].kingCell) {
                    board[x][y].ownerId = 1;
                    board[x][y].isExplored = true;
                    board[x][y].isVisible = true;
                }
            }
        }
        
        // Территория игрока 2 (правый нижний угол)
        for (int x = size - territorySize; x < size; ++x) {
            for (int y = size - territorySize; y < size; ++y) {
                if (!board[x][y].kingCell) {
                    board[x][y].ownerId = 2;
                    board[x][y].isExplored = true;
                    board[x][y].isVisible = true;
                }
            }
        }
    }
    
    void addSabotageCells() {
        int numSabotage = std::max(minSabotage, (size * size) / sabotageDivisor);
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(0, size-1);
        std::uniform_int_distribution<> pointsDistrib(2, 5);
        
        int placed = 0;
        int attempts = 0;
        while (placed < numSabotage && attempts < size * size * 2) {
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
        // Обновляем видимость перед обновлением доступных ходов
        updateVisibility();
        
        for (auto& row : board) {
            for (auto& cell : row) {
                cell.isAvailable = false;
            }
        }
        
        int playerId = currentPlayer + 1;
        
        for (int x = 0; x < size; ++x) {
            for (int y = 0; y < size; ++y) {
                if (board[x][y].ownerId == playerId && board[x][y].isVisible && !board[x][y].isFortified) {
                    const int dirs[4][2] = {{-1,0}, {1,0}, {0,-1}, {0,1}};
                    for (int d = 0; d < 4; ++d) {
                        int nx = x + dirs[d][0];
                        int ny = y + dirs[d][1];
                        if (nx >= 0 && nx < size && ny >= 0 && ny < size) {
                            Cell& neighbor = board[nx][ny];
                            if (neighbor.ownerId != playerId && !neighbor.kingCell && 
                                neighbor.isVisible && !neighbor.isFortified) {
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
        
        if (player.abilityUsedThisTurn) {
            std::cout << "❌ Вы уже использовали способность в этом ходу!\n";
            ColorManager::waitForEnter();
            return false;
        }
        
        int cursorX = player.cursorX;
        int cursorY = player.cursorY;
        
        if (!canCapture(cursorX, cursorY)) {
            std::cout << "❌ Нельзя захватить эту клетку!\n";
            ColorManager::waitForEnter();
            return false;
        }
        
        Cell& cell = board[cursorX][cursorY];
        
        // Проверяем, не укреплена ли клетка
        if (cell.isFortified) {
            std::cout << "❌ Нельзя захватить укрепленную клетку! Используйте артиллерию.\n";
            ColorManager::waitForEnter();
            return false;
        }
        
        int sabotagePoints = 0;
        if (cell.sabotageCell) {
            sabotagePoints = cell.sabotageValue;
            cell.sabotageCell = false;
            cell.sabotageValue = 0;
        }
        
        int previousOwner = cell.ownerId;
        cell.ownerId = static_cast<uint8_t>(currentPlayer + 1);
        cell.isExplored = true;
        cell.isVisible = true;
        
        int pointsEarned = (previousOwner == 0) ? 1 : 2;
        player.score += pointsEarned + sabotagePoints;
        
        std::cout << "✅ Клетка захвачена! ";
        if (sabotagePoints > 0) {
            std::cout << "+" << sabotagePoints << " за диверсию! ";
        }
        std::cout << ((previousOwner == 0) ? "+1 очко" : "+2 очка") << "\n";
        
        if (cell.kingCell && previousOwner != currentPlayer + 1) {
            gameOver = true;
            winner = currentPlayer + 1;
        }
        
        // Захватываем окруженные территории
        captureSurroundedTerritories();
        captureSurroundedNeutralTerritories();
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
        
        if (player.abilityUsedThisTurn) {
            std::cout << " [✋ СПОСОБНОСТЬ ИСПОЛЬЗОВАНА]";
        } else {
            std::cout << " [✅ СПОСОБНОСТЬ ДОСТУПНА]";
        }
        
        std::cout << "\n";
        
        std::cout << "📊 Счет: ";
        std::cout << ColorManager::get(2) << " Игрок1=" << players[0].score << " " << ColorManager::get(1);
        std::cout << " | ";
        std::cout << ColorManager::get(3) << " Игрок2=" << players[1].score << " " << ColorManager::get(1);
        std::cout << "\n";
        std::cout << "💎 Ваши очки: " << player.score << "\n";
        std::cout << "👁️ Видимость: " << visibilityRadius << " клетки от ваших территорий\n";
        std::cout << "📏 Размер поля: " << size << "x" << size << "\n\n";
        
        // Для больших полей показываем только часть вокруг курсора
        int displaySize = std::min(size, 20); // Максимум 20 клеток для отображения
        int startX = std::max(0, static_cast<int>(cursorX) - displaySize/2);
        int startY = std::max(0, static_cast<int>(cursorY) - displaySize/2);
        int endX = std::min(size, startX + displaySize);
        int endY = std::min(size, startY + displaySize);
        
        if (size > 20) {
            std::cout << "📋 Показана область " << startX << "," << startY 
                      << " - " << endX-1 << "," << endY-1 
                      << " (все поле " << size << "x" << size << ")\n";
            std::cout << "📍 Курсор в центре области (" << cursorX << "," << cursorY << ")\n";
        }
        
        std::cout << "   ";
        for (int i = startX; i < endX; ++i) std::cout << i % 10 << " ";
        std::cout << "\n";
        
        for (int y = startY; y < endY; ++y) {
            std::cout << ColorManager::get(1) << y % 10 << " ";
            for (int x = startX; x < endX; ++x) {
                const Cell& cell = board[x][y];
                
                // Проверяем видимость
                if (!cell.isVisible) {
                    std::cout << ColorManager::get(9) << "? " << ColorManager::get(0);
                    continue;
                }
                
                if (x == cursorX && y == cursorY) {
                    std::cout << ColorManager::get(8);
                }
                else if (cell.isAvailable) {
                    std::cout << ColorManager::get(7);
                }
                else if (cell.isFortified) {
                    std::cout << ColorManager::get(10); // Коричневый для укреплений
                }
                else if (cell.ownerId == 1) {
                    std::cout << ColorManager::get(2);
                }
                else if (cell.ownerId == 2) {
                    std::cout << ColorManager::get(3);
                }
                else {
                    std::cout << ColorManager::get(6);
                }
                
                if (cell.kingCell) {
                    std::cout << (cell.ownerId == 1 ? "K" : "Q");
                } else if (cell.sabotageCell) {
                    std::cout << "O";
                } else if (cell.isFortified) {
                    std::cout << "S"; // S для укреплений (Stronghold)
                } else if (cell.ownerId == 1) {
                    std::cout << "1";
                } else if (cell.ownerId == 2) {
                    std::cout << "2";
                } else {
                    std::cout << ".";
                }
                
                std::cout << " " << ColorManager::get(0);
            }
            std::cout << ColorManager::get(1) << "\n";
        }
        
        std::cout << "\n🎯 Управление: WASD - движение, Space - выбрать, E - способности, P - пропуск\n";
        std::cout << "📍 Курсор Игрока " << playerId << ": (" << cursorX << "," << cursorY << ")";
        
        if (board[cursorX][cursorY].isVisible && board[cursorX][cursorY].isAvailable) {
            std::cout << " ✅ Доступно для захвата";
        } else if (!board[cursorX][cursorY].isVisible) {
            std::cout << " ❌ Невидимая клетка";
        } else if (board[cursorX][cursorY].isFortified) {
            std::cout << " 🏰 Укрепленная клетка (S)";
        }
        
        std::cout << "\n👁️ Клетки с '?' невидимы (радиус видимости: " << visibilityRadius << " клетки)\n";
        std::cout << "🔄 Нейтральные территории, окруженные одним игроком, захватываются автоматически!\n";
        std::cout << "💣 Кассетная бомба: 2x2 | 🏰 Укрепления: 1x2 (только артиллерия, обозначение: S)\n";
        std::cout << ColorManager::get(0);
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
        
        if (player.abilityUsedThisTurn) {
            std::cout << " [✋ УЖЕ ИСПОЛЬЗОВАНА В ЭТОМ ХОДУ]\n\n";
        } else {
            std::cout << " [✅ ДОСТУПНА]\n\n";
        }
        
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
            
            if (player.abilityUsedThisTurn) {
                std::cout << " ❌ НЕДОСТУПНО (способность уже использована)";
            } else if (!player.canUseAbility(ability.baseCost)) {
                std::cout << " ❌ НЕДОСТУПНО (недостаточно очков)";
            } else {
                std::cout << " ✅ ДОСТУПНО";
            }
            
            std::cout << "\n   " << ability.description << "\n";
            
            if (abilitiesUsed[i] > 0) {
                std::cout << "   Использовано: " << abilitiesUsed[i] << " раз\n";
            }
            std::cout << "\n";
        }
        
        if (player.abilityUsedThisTurn) {
            std::cout << "\n⚠️ Вы уже использовали способность в этом ходу!\n";
            std::cout << "Нажмите любую клавишу для возврата..." << ColorManager::get(0);
            _getch();
            return;
        }
        
        std::cout << "Выберите способность (1-7) или 0 для отмены: " << ColorManager::get(0);
    }
    
    bool useParatrooper(int x, int y) {
        std::cout << "📍 Использование Десантника на клетке (" << x << "," << y << ")\n";
        
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
        
        if (board[x][y].isFortified) {
            std::cout << "❌ Нельзя поставить десантника на укрепленную клетку!\n";
            return false;
        }
        
        if (board[x][y].sabotageCell) {
            players[currentPlayer].score += board[x][y].sabotageValue;
            board[x][y].sabotageCell = false;
            board[x][y].sabotageValue = 0;
        }
        board[x][y].ownerId = static_cast<uint8_t>(currentPlayer + 1);
        board[x][y].isExplored = true;
        board[x][y].isVisible = true;
        return true;
    }
    
    bool useClusterBomb(int x, int y) {
        std::cout << "💣 Использование Кассетной бомбы на клетке (" << x << "," << y << ")\n";
        std::cout << "💥 Область поражения: 2x2 клетки\n";
        
        int cellsDestroyed = 0;
        // Область 2x2
        for (int dx = 0; dx <= 1; ++dx) {
            for (int dy = 0; dy <= 1; ++dy) {
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < size && ny >= 0 && ny < size && !board[nx][ny].kingCell) {
                    if (board[nx][ny].isFortified) {
                        std::cout << "💥 Укрепление (S) разрушено в клетке (" << nx << "," << ny << ")\n";
                        board[nx][ny].isFortified = false;
                    }
                    
                    if (board[nx][ny].sabotageCell) {
                        std::cout << "💥 Саботажная клетка уничтожена в (" << nx << "," << ny << ")\n";
                        board[nx][ny].sabotageCell = false;
                        board[nx][ny].sabotageValue = 0;
                    }
                    
                    board[nx][ny].ownerId = 0;
                    board[nx][ny].isExplored = true;
                    board[nx][ny].isVisible = true;
                    cellsDestroyed++;
                }
            }
        }
        
        std::cout << "✅ Уничтожено " << cellsDestroyed << " клеток в области 2x2\n";
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
                // Проверяем, не укреплена ли клетка
                if (board[nx][ny].isFortified) {
                    std::cout << "❌ Нельзя захватить укрепленную клетку (S) (" << nx << "," << ny << ")!\n";
                    continue;
                }
                
                if (board[nx][ny].sabotageCell) {
                    players[currentPlayer].score += board[nx][ny].sabotageValue;
                    board[nx][ny].sabotageCell = false;
                    board[nx][ny].sabotageValue = 0;
                }
                board[nx][ny].ownerId = static_cast<uint8_t>(playerId);
                board[nx][ny].isExplored = true;
                board[nx][ny].isVisible = true;
                
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
        std::cout << "💥 Область поражения: 3x3 клетки\n";
        
        int cellsDestroyed = 0;
        int fortificationsDestroyed = 0;
        
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < size && ny >= 0 && ny < size && !board[nx][ny].kingCell) {
                    // Уничтожаем укрепления
                    if (board[nx][ny].isFortified) {
                        board[nx][ny].isFortified = false;
                        fortificationsDestroyed++;
                        std::cout << "💥 Укрепление (S) разрушено в (" << nx << "," << ny << ")\n";
                    }
                    
                    if (board[nx][ny].sabotageCell) {
                        board[nx][ny].sabotageCell = false;
                        board[nx][ny].sabotageValue = 0;
                    }
                    
                    board[nx][ny].ownerId = 0;
                    board[nx][ny].isExplored = true;
                    board[nx][ny].isVisible = true;
                    cellsDestroyed++;
                }
            }
        }
        
        std::cout << "✅ Уничтожено " << cellsDestroyed << " клеток в области 3x3\n";
        if (fortificationsDestroyed > 0) {
            std::cout << "💥 Разрушено " << fortificationsDestroyed << " укреплений (S)\n";
        }
        return true;
    }
    
    bool useFortifications(int x, int y) {
        std::cout << "\n🏰 Использование Укреплений на клетке (" << x << "," << y << ")\n";
        std::cout << "Цена: " << (players[currentPlayer].commanderActive ? 
                                  Constants::FORTIFICATION_COST * (100 - Constants::COMMANDER_DISCOUNT_PERCENT) / 100 : 
                                  Constants::FORTIFICATION_COST) << " очков\n";
        std::cout << "Выберите направление для укрепления 1x2 (W-вверх, S-вниз, A-влево, D-вправо): ";
        
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
                ColorManager::waitForEnter();
                return false;
        }
        
        int playerId = currentPlayer + 1;
        
        // Проверяем обе клетки
        for (int i = 0; i < 2; ++i) {
            int nx = x + dx * i;
            int ny = y + dy * i;
            
            // Проверка границ
            if (nx < 0 || nx >= size || ny < 0 || ny >= size) {
                std::cout << "❌ Клетка (" << nx << "," << ny << ") выходит за границы поля!\n";
                ColorManager::waitForEnter();
                return false;
            }
            
            // Проверка владельца
            if (board[nx][ny].ownerId != playerId) {
                std::cout << "❌ Клетка (" << nx << "," << ny << ") не принадлежит вам!\n";
                ColorManager::waitForEnter();
                return false;
            }
            
            // Проверка на укрепление
            if (board[nx][ny].isFortified) {
                std::cout << "❌ Клетка (" << nx << "," << ny << ") уже укреплена!\n";
                ColorManager::waitForEnter();
                return false;
            }
            
            // Проверка на королевскую клетку
            if (board[nx][ny].kingCell) {
                std::cout << "❌ Нельзя укреплять королевскую клетку!\n";
                ColorManager::waitForEnter();
                return false;
            }
        }
        
        // Всё в порядке, устанавливаем укрепления
        for (int i = 0; i < 2; ++i) {
            int nx = x + dx * i;
            int ny = y + dy * i;
            
            board[nx][ny].isFortified = true;
            board[nx][ny].isExplored = true;
            board[nx][ny].isVisible = true;
            
            // Убираем саботажные клетки, если они есть
            if (board[nx][ny].sabotageCell) {
                board[nx][ny].sabotageCell = false;
                board[nx][ny].sabotageValue = 0;
            }
        }
        
        std::cout << "✅ Укрепления (S) установлены в направлении " << direction << "!\n";
        std::cout << "🏰 Клетки (" << x << "," << y << ") и (" 
                  << (x + dx) << "," << (y + dy) << ") теперь укреплены (S)\n";
        std::cout << "⚠️ Укрепления (S) нельзя захватить обычным способом, только артиллерией!\n";
        return true;
    }
    
    bool useScouting(int x, int y) {
        std::cout << "🔍 Разведка активирована! Показана область " 
                  << (scoutingRadius*2+1) << "x" 
                  << (scoutingRadius*2+1) 
                  << " вокруг (" << x << "," << y << ")\n";
        
        for (int dx = -scoutingRadius; dx <= scoutingRadius; ++dx) {
            for (int dy = -scoutingRadius; dy <= scoutingRadius; ++dy) {
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < size && ny >= 0 && ny < size) {
                    board[nx][ny].isVisible = true;
                    board[nx][ny].isExplored = true;
                    board[nx][ny].lastSeenOwner = board[nx][ny].ownerId;
                }
            }
        }
        
        std::cout << "Область теперь исследована!\n";
        return true;
    }
    
    bool useAbility(int abilityIndex) {
        Player& player = players[currentPlayer];
        
        if (player.abilityUsedThisTurn) {
            std::cout << "❌ Вы уже использовали способность в этом ходу!\n";
            std::cout << "Можно использовать только одну способность за ход.\n";
            ColorManager::waitForEnter();
            return false;
        }
        
        int cursorX = player.cursorX;
        int cursorY = player.cursorY;
        
        if (abilityIndex < 0 || abilityIndex >= NUM_ABILITIES) {
            std::cout << "❌ Неверный выбор способности!\n";
            ColorManager::waitForEnter();
            return false;
        }
        
        const auto& ability = ABILITIES[abilityIndex];
        if (!player.canUseAbility(ability.baseCost)) {
            std::cout << "❌ Недостаточно очков или способность уже использована!\n";
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
            std::cout << "⚠️ Теперь вы не можете использовать другие способности в этом ходу.\n";
            
            // После использования способности обновляем состояние
            captureSurroundedTerritories();
            captureSurroundedNeutralTerritories();
            updateAvailableMoves();
            ColorManager::waitForEnter();
        }
        
        return success;
    }
    
    void abilitiesMenu() {
        Player& player = players[currentPlayer];
        
        if (player.abilityUsedThisTurn) {
            std::cout << "❌ Вы уже использовали способность в этом ходу!\n";
            std::cout << "Можно использовать только одну способность за ход.\n";
            ColorManager::waitForEnter();
            return;
        }
        
        displayAbilities();
        
        if (player.abilityUsedThisTurn) {
            return;
        }
        
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
        players[currentPlayer].resetTurn();
        
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
                    updateVisibility(); // Обновляем видимость при движении
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
        
        // Подсчет укреплений
        int fortifications1 = 0, fortifications2 = 0;
        for (int x = 0; x < size; ++x) {
            for (int y = 0; y < size; ++y) {
                if (board[x][y].isFortified) {
                    if (board[x][y].ownerId == 1) fortifications1++;
                    else if (board[x][y].ownerId == 2) fortifications2++;
                }
            }
        }
        
        std::cout << "\n🏰 Укрепления на поле (обозначение: S):\n";
        std::cout << "Игрок 1: " << fortifications1 << " укрепленных клеток\n";
        std::cout << "Игрок 2: " << fortifications2 << " укрепленных клеток\n";
        
        std::cout << "\n🏆 Победитель: Игрок " << winner << "!\n";
        
        if (winner > 0) {
            std::cout << "🎉 Поздравляем Игрока " << winner << " с победой!\n";
        }
        
        std::cout << "\n📏 Размер поля: " << size << "x" << size << "\n";
        ColorManager::waitForEnter();
    }
    
public:
    Game(int s) : currentPlayer(0), gameOver(false), winner(0) {
        size = s;
        setGameParameters(); // Устанавливаем параметры в зависимости от размера
        
        for (int i = 0; i < NUM_ABILITIES; ++i) {
            abilitiesUsed[i] = 0;
        }
        
        // Располагаем королевские клетки в противоположных углах
        players[0] = Player(1, 0, 0);
        players[1] = Player(2, size-1, size-1);
        
        board = std::vector<std::vector<Cell>>(size, std::vector<Cell>(size));
        
        // Инициализация королевских клеток
        board[0][0].kingCell = true;
        board[0][0].ownerId = 1;
        board[0][0].isVisible = true;
        board[0][0].isExplored = true;
        
        board[size-1][size-1].kingCell = true;
        board[size-1][size-1].ownerId = 2;
        board[size-1][size-1].isVisible = true;
        board[size-1][size-1].isExplored = true;
        
        createInitialTerritories();
        addSabotageCells();
        updateAvailableMoves();
    }
    
    void start() {
        std::cout << ColorManager::get(1) << "\n=== Добро пожаловать в Cell Warfare! ===\n";
        std::cout << "🎮 ОБНОВЛЕННЫЕ СПОСОБНОСТИ:\n";
        std::cout << "💣 КАССЕТНАЯ БОМБА: теперь действует на область 2x2 клетки\n";
        std::cout << "🏰 УКРЕПЛЕНИЯ: 2 клетки в выбранном направлении, цена 6 очков\n";
        std::cout << "   - Обозначение: S (Stronghold)\n";
        std::cout << "   - Нельзя захватить обычным способом\n";
        std::cout << "   - Уничтожаются только артиллерией\n";
        std::cout << "👁️  ТУМАН ВОЙНЫ: Видно только клетки в радиусе " 
                  << visibilityRadius << " от ваших территорий\n";
        std::cout << "🔄 АВТОЗАХВАТ: Нейтральные территории, окруженные одним игроком, захватываются автоматически\n";
        std::cout << "✋ ОГРАНИЧЕНИЕ: только одна способность за ход!\n";
        std::cout << "📏 РАЗМЕР ПОЛЯ: " << size << "x" << size << "\n";
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
    ColorManager::clearScreen();
    
    std::cout << ColorManager::get(1) << "=== CELL WARFARE ===\n";
    std::cout << "ОБНОВЛЕННАЯ ВЕРСИЯ с новыми способностями!\n\n";
    
    std::cout << "🎮 ВЫБЕРИТЕ РАЗМЕР ПОЛЯ:\n";
    std::cout << "1. 🟦 МАЛЕНЬКОЕ (16x16) - быстрая игра\n";
    std::cout << "2. 🟧 СРЕДНЕЕ (32x32) - сбалансированная игра\n";
    std::cout << "3. 🟥 БОЛЬШОЕ (64x64) - эпическая битва\n\n";
    
    std::cout << "НОВИНКИ:\n";
    std::cout << "💣 Кассетная бомба - теперь 2x2 клетки\n";
    std::cout << "🏰 Укрепления - 2 клетки, цена 6, обозначение S, только артиллерия\n\n";
    
    std::cout << "Выберите размер (1-3): " << ColorManager::get(0);
    
    int choice = 0;
    std::string input;
    std::getline(std::cin, input);
    
    if (!input.empty()) {
        try {
            choice = std::stoi(input);
        } catch (...) {
            choice = 1;
        }
    }
    
    int size;
    switch (choice) {
        case 1:
            size = Constants::BOARD_SIZE_SMALL;
            std::cout << "\n✅ Выбрано маленькое поле 16x16\n";
            break;
        case 2:
            size = Constants::BOARD_SIZE_MEDIUM;
            std::cout << "\n✅ Выбрано среднее поле 32x32\n";
            break;
        case 3:
            size = Constants::BOARD_SIZE_LARGE;
            std::cout << "\n✅ Выбрано большое поле 64x64\n";
            break;
        default:
            size = Constants::BOARD_SIZE_SMALL;
            std::cout << "\n✅ Выбрано маленькое поле 16x16 (по умолчанию)\n";
            break;
    }
    
    std::cout << "👁️ Радиус видимости: " << 
        (size == 16 ? Constants::VISIBILITY_RADIUS_SMALL : 
         size == 32 ? Constants::VISIBILITY_RADIUS_MEDIUM : 
         Constants::VISIBILITY_RADIUS_LARGE) << " клетки\n";
    std::cout << "🔄 Автозахват окруженных территорий: ВКЛЮЧЕН\n";
    std::cout << "💣 Кассетная бомба: область 2x2 клетки\n";
    std::cout << "🏰 Укрепления: цена " << Constants::FORTIFICATION_COST << " очков, обозначение S\n\n";
    
    Game game(size);
    game.start();
    
    return 0;
}