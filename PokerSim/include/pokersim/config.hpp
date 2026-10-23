// Configuration d'une simulation : formats Cash, SNG et Spin & Go.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ps {

enum class GameFormat { Cash, SNG, SpinGo };
enum class LevelBy { Time, Hands };
enum class CashStackMode { Rebuy, Reset };

struct BlindLevel { int sb = 10, bb = 20, ante = 0; };

struct PlayerSpec {
    std::string name;
    std::string profile;   // "builtin:tag", "builtin:call", "builtin:random" ou chemin .ohf/.oppl/.txt (phase 2)
};

struct SimConfig {
    GameFormat format = GameFormat::SpinGo;
    int numPlayers = 3;           // Cash 2..10, SNG 2..10, Spin 2..3
    int numGames = 100;           // parties (SNG/Spin) ou sessions (Cash)
    uint64_t seed = 12345;
    int startingStack = 500;
    double buyin = 1.0;           // en unités monétaires, pour SNG/Spin (frais inclus)
    double feePct = 0.0;          // part du prize pool retenue (SNG)
    bool rotateSeats = true;

    // Blinds : structure montante (SNG / Spin)
    std::vector<BlindLevel> levels;
    LevelBy levelBy = LevelBy::Time;
    int levelSeconds = 90;        // si levelBy == Time
    int levelHands = 10;          // si levelBy == Hands
    int maxHandsPerGame = 1000;

    // Temps simulé d'une main (utilisé pour la montée des blinds au temps)
    double handBaseSeconds = 10.0, actionSeconds = 3.0, showdownSeconds = 4.0;

    // Cash game
    int cashSb = 5, cashBb = 10, cashAnte = 0;
    int cashHands = 1000;         // mains par session
    int cashStack = 1000;
    CashStackMode cashStackMode = CashStackMode::Rebuy;
    double chipValue = 0.01;      // valeur monétaire d'un jeton
    double rakePct = 5.0;
    int rakeCap = 0;              // 0 = sans plafond
    bool noFlopNoDrop = true;

    // Gains SNG : parts par place (vide = défaut selon le nombre de joueurs)
    std::vector<double> payoutShares;
    // Spin & Go : (multiplicateur, poids)
    std::vector<std::pair<double, int>> multipliers;
    bool multiplierTimesTotal = false;   // false : prize pool = mult x buy-in ; true : mult x buy-in x joueurs

    // Calcul de prwin : itérations par défaut (le profil peut en demander plus, plafonné)
    int prwinIterations = 1000;
    int prwinMaxIterations = 0;   // 0 = aucune limite (comme OpenHoldem) ; > 0 plafonne f$prwin_number_of_iterations pour la vitesse

    std::vector<PlayerSpec> players;
    int workers = 0;              // 0 = auto
    std::string outputDir = "output";
    bool writeHandHistory = false;
    std::string handHistoryPath;  // si non vide : toutes les mains sont écrites dans ce fichier texte (UTF-8), au fil de la simulation
    int handHistoryGames = 0;     // limite le fichier aux N premières parties (0 = toutes)
    // PokerTracker simulé : statistiques des adversaires lues par les symboles pt_*
    bool ptEnabled = true;
    std::string ptBaselinePath;   // base JSON de départ (statistiques déjà connues sur les adversaires), facultative
    std::string ptSavePath;       // si non vide : base de départ + toutes les mains de la simulation, écrite à la fin
    int recordGames = 0;          // nombre de premières parties dont on conserve l'historique des mains (0 = aucune)

    bool isCash() const { return format == GameFormat::Cash; }
    // Valeurs par défaut (niveaux, multiplicateurs...) et validation ; lève std::runtime_error
    void finalize();
    static SimConfig fromJsonFile(const std::string& path);
    static SimConfig fromJsonText(const std::string& text);
    std::string toJsonText() const;
};

std::vector<BlindLevel> defaultSpinLevels();
std::vector<BlindLevel> defaultSngLevels();
std::vector<std::pair<double, int>> defaultSpinMultipliers();
std::vector<double> defaultPayoutShares(int numPlayers);

const char* formatName(GameFormat f);

}  // namespace ps
