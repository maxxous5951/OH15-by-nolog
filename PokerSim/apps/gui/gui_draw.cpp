// Dessin de l'interface (Dear ImGui). Les textes n'utilisent que de l'ASCII et du Latin-1 (police ImGui par défaut).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <thread>
#include "gui_app.hpp"
#include "imgui.h"
#include "misc/imgui_stdlib.h"

namespace psgui {
namespace {

// Taille en « em » : proportionnelle à la police, donc à l'échelle DPI choisie par la plateforme (17 px = 1.0)
float em(float px) { return px * ImGui::GetFontSize() / 17.0f; }

ImU32 palette(int i) {
    static const ImU32 c[10] = {IM_COL32(86, 180, 233, 255), IM_COL32(230, 159, 0, 255), IM_COL32(0, 158, 115, 255), IM_COL32(213, 94, 0, 255),
                                IM_COL32(204, 121, 167, 255), IM_COL32(240, 228, 66, 255), IM_COL32(0, 114, 178, 255), IM_COL32(150, 150, 150, 255),
                                IM_COL32(170, 110, 220, 255), IM_COL32(120, 200, 90, 255)};
    return c[i % 10];
}

bool inputIntClamped(const char* label, int* v, int lo, int hi, int step = 1) {
    bool ch = ImGui::InputInt(label, v, step, step * 10);
    *v = std::clamp(*v, lo, hi);
    return ch;
}
bool inputDouble(const char* label, double* v, double lo, double hi, const char* fmt = "%.2f") {
    bool ch = ImGui::InputDouble(label, v, 0.0, 0.0, fmt);
    *v = std::clamp(*v, lo, hi);
    return ch;
}
void help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f); ImGui::TextUnformatted(text); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
}

// Histogramme : une barre par joueur, avec intervalle de confiance
void drawBars(const char* id, const std::vector<std::string>& labels, const std::vector<double>& vals, const std::vector<double>& errs, const char* unit, float height) {
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = std::max(50.0f, ImGui::GetContentRegionAvail().x);
    ImGui::InvisibleButton(id, ImVec2(w, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + height), IM_COL32(25, 25, 30, 255));
    const int n = static_cast<int>(vals.size());
    if (!n) return;
    double lo = 0, hi = 0;
    for (int i = 0; i < n; ++i) { lo = std::min(lo, vals[i] - errs[i]); hi = std::max(hi, vals[i] + errs[i]); }
    if (hi - lo < 1e-9) hi = lo + 1;
    const double pad = (hi - lo) * 0.12;
    lo -= pad; hi += pad;
    const float top = p0.y + 16.0f, bottom = p0.y + height - 22.0f;
    auto yOf = [&](double v) { return static_cast<float>(bottom - (v - lo) / (hi - lo) * (bottom - top)); };
    const float y0 = yOf(0.0);
    dl->AddLine(ImVec2(p0.x, y0), ImVec2(p0.x + w, y0), IM_COL32(120, 120, 120, 255));
    dl->AddText(ImVec2(p0.x + 4, p0.y + 1), IM_COL32(170, 170, 170, 255), unit);
    const float slot = w / static_cast<float>(n), bw = std::min(80.0f, slot * 0.55f);
    for (int i = 0; i < n; ++i) {
        const float cx = p0.x + slot * (static_cast<float>(i) + 0.5f), y = yOf(vals[i]);
        dl->AddRectFilled(ImVec2(cx - bw / 2, std::min(y, y0)), ImVec2(cx + bw / 2, std::max(y, y0)), palette(i));
        if (errs[i] > 0) {
            const float ya = yOf(vals[i] + errs[i]), yb = yOf(vals[i] - errs[i]);
            dl->AddLine(ImVec2(cx, ya), ImVec2(cx, yb), IM_COL32(255, 255, 255, 255), 1.5f);
            dl->AddLine(ImVec2(cx - 6, ya), ImVec2(cx + 6, ya), IM_COL32(255, 255, 255, 255), 1.5f);
            dl->AddLine(ImVec2(cx - 6, yb), ImVec2(cx + 6, yb), IM_COL32(255, 255, 255, 255), 1.5f);
        }
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.1f", vals[i]);
        dl->AddText(ImVec2(cx - 14, vals[i] >= 0 ? yOf(vals[i] + errs[i]) - 15 : yOf(vals[i] - errs[i]) + 2), IM_COL32(230, 230, 230, 255), buf);
        dl->AddText(ImVec2(cx - bw / 2, p0.y + height - 18), IM_COL32(200, 200, 200, 255), labels[i].c_str());
    }
}

// Courbes de gains cumulés
void drawLines(const char* id, const std::vector<std::vector<float>>& series, const std::vector<std::string>& names, float height) {
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = std::max(50.0f, ImGui::GetContentRegionAvail().x);
    ImGui::InvisibleButton(id, ImVec2(w, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + height), IM_COL32(25, 25, 30, 255));
    float lo = 0, hi = 0;
    size_t maxN = 0;
    for (auto& s : series) { maxN = std::max(maxN, s.size()); for (float v : s) { lo = std::min(lo, v); hi = std::max(hi, v); } }
    if (maxN < 2) return;
    if (hi - lo < 1e-6f) hi = lo + 1;
    const float pad = (hi - lo) * 0.08f;
    lo -= pad; hi += pad;
    const float left = p0.x + 8, right = p0.x + w - 8, top = p0.y + 22, bottom = p0.y + height - 8;
    auto yOf = [&](float v) { return bottom - (v - lo) / (hi - lo) * (bottom - top); };
    dl->AddLine(ImVec2(left, yOf(0)), ImVec2(right, yOf(0)), IM_COL32(120, 120, 120, 255));
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.1f", hi); dl->AddText(ImVec2(left, p0.y + 22), IM_COL32(150, 150, 150, 255), buf);
    std::snprintf(buf, sizeof buf, "%.1f", lo); dl->AddText(ImVec2(left, bottom - 16), IM_COL32(150, 150, 150, 255), buf);
    float lx = left;
    for (size_t k = 0; k < series.size(); ++k) {
        const auto& s = series[k];
        if (s.size() >= 2) {
            const size_t step = std::max<size_t>(1, s.size() / 700);
            std::vector<ImVec2> pts;
            for (size_t i = 0; i < s.size(); i += step) pts.push_back(ImVec2(left + (right - left) * static_cast<float>(i) / static_cast<float>(maxN - 1), yOf(s[i])));
            pts.push_back(ImVec2(left + (right - left) * static_cast<float>(s.size() - 1) / static_cast<float>(maxN - 1), yOf(s.back())));
            dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), palette(static_cast<int>(k)), 0, 2.0f);
        }
        dl->AddRectFilled(ImVec2(lx, p0.y + 5), ImVec2(lx + 10, p0.y + 15), palette(static_cast<int>(k)));
        dl->AddText(ImVec2(lx + 14, p0.y + 2), IM_COL32(210, 210, 210, 255), names[k].c_str());
        lx += 28.0f + ImGui::CalcTextSize(names[k].c_str()).x;
    }
}

}  // namespace

// ============================================================ fenêtre principale
void App::frame() {
    poll();
    checkOhRoot();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(em(0), em(0)));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("PokerSim", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    drawTopBar();
    if (ImGui::BeginTabBar("tabs")) {
        auto tab = [&](int idx, const char* name, void (App::*fn)()) {
            if (ImGui::BeginTabItem(name, nullptr, requestedTab == idx ? ImGuiTabItemFlags_SetSelected : 0)) { (this->*fn)(); ImGui::EndTabItem(); }
        };
        tab(0, "Configuration", &App::drawConfigTab);
        tab(1, "Joueurs", &App::drawPlayersTab);
        tab(2, "Resultats", &App::drawResultsTab);
        tab(3, "Historique des mains", &App::drawHandsTab);
        ImGui::EndTabBar();
        requestedTab = -1;
    }
    ImGui::End();
}

void App::drawTopBar() {
    ImGui::BeginDisabled(running_);
    if (ImGui::Button("Lancer la simulation", ImVec2(em(200), em(30)))) startRun();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!running_);
    if (ImGui::Button("Arreter", ImVec2(em(100), em(30)))) stopRun();
    ImGui::EndDisabled();
    ImGui::SameLine();
    const int done = done_.load(), total = std::max(1, total_.load());
    char overlay[160];
    if (running_) {
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
        std::snprintf(overlay, sizeof overlay, "%d / %d (%.0f%%)  -  %.0f s%s", done, total, 100.0 * done / total, el, cancel_ ? "  -  arret en cours..." : "");
    } else if (results_) {
        std::snprintf(overlay, sizeof overlay, "Termine : %zu %s, %lld mains en %.1f s (%.0f mains/s)%s", results_->summary.games.size(),
                      results_->cfg.isCash() ? "sessions" : "parties", results_->totalHands, results_->seconds,
                      results_->seconds > 0 ? static_cast<double>(results_->totalHands) / results_->seconds : 0.0, results_->cancelled ? " - interrompu" : "");
    } else std::snprintf(overlay, sizeof overlay, "Pret");
    ImGui::ProgressBar(running_ ? static_cast<float>(done) / static_cast<float>(total) : (results_ ? 1.0f : 0.0f), ImVec2(-1, em(30)), overlay);
    if (!error_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "Erreur : %s", error_.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Fermer##err")) error_.clear();
    }
    ImGui::Separator();
}

// ============================================================ configuration
void App::drawConfigTab() {
    ImGui::BeginDisabled(running_);
    ImGui::BeginChild("cfg", ImVec2(em(0), em(0)), ImGuiChildFlags_None);
    ImGui::SeparatorText("Format de jeu");
    static const char* formats[] = {"Cash game", "Sit & Go (SNG)", "Spin & Go"};
    int f = cfg.format == ps::GameFormat::Cash ? 0 : cfg.format == ps::GameFormat::SNG ? 1 : 2;
    ImGui::SetNextItemWidth(em(260));
    if (ImGui::Combo("Format", &f, formats, 3)) setFormat(f == 0 ? ps::GameFormat::Cash : f == 1 ? ps::GameFormat::SNG : ps::GameFormat::SpinGo);
    help("Changer de format rétablit les blinds, gains et multiplicateurs par défaut de ce format.");
    const int maxP = cfg.format == ps::GameFormat::SpinGo ? 3 : 10;
    ImGui::SetNextItemWidth(em(260));
    const int prevP = cfg.numPlayers;
    inputIntClamped("Nombre de joueurs", &cfg.numPlayers, 2, maxP);
    if (cfg.numPlayers != prevP && cfg.format != ps::GameFormat::Cash && static_cast<int>(cfg.payoutShares.size()) > cfg.numPlayers) cfg.payoutShares = ps::defaultPayoutShares(cfg.numPlayers);
    ImGui::SetNextItemWidth(em(260));
    inputIntClamped(cfg.isCash() ? "Sessions" : "Parties", &cfg.numGames, 1, 10000000, 10);
    help(cfg.isCash() ? "Une session = un nombre fixe de mains avec les mêmes joueurs (voir plus bas)." : "Une partie = un tournoi complet jusqu'au dernier joueur.");
    ImGui::SetNextItemWidth(em(260));
    ImGui::InputScalar("Graine aléatoire", ImGuiDataType_U64, &cfg.seed);
    ImGui::SameLine();
    if (ImGui::Button("Aleatoire")) cfg.seed = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    help("Même graine + même configuration = mêmes résultats, quel que soit le nombre de threads.");
    ImGui::Checkbox("Changer les places à chaque partie", &cfg.rotateSeats);
    ImGui::SetNextItemWidth(em(260));
    inputIntClamped("Threads de calcul (0 = auto)", &cfg.workers, 0, 256);
    ImGui::SameLine();
    ImGui::TextDisabled("%u coeurs detectes", std::thread::hardware_concurrency());

    drawFormatSpecific();

    ImGui::SeparatorText("Calcul de prwin");
    ImGui::SetNextItemWidth(em(260));
    inputIntClamped("Plafond d'itérations (0 = aucun)", &cfg.prwinMaxIterations, 0, 10000000, 500);
    help("0 : fidèle à OpenHoldem, le profil décide (la bibliothèque demande 50 000 itérations, c'est lent).\nUne valeur de 1 000 à 5 000 accélère beaucoup la simulation au prix d'un peu de bruit.");

    ImGui::SeparatorText("Historique des mains");
    ImGui::SetNextItemWidth(em(260));
    inputIntClamped("Enregistrer les N premieres parties", &cfg.recordGames, 0, 50);
    help("Consultable dans l'onglet Historique des mains. 0 = aucun (plus rapide, moins de mémoire).");

    ImGui::SeparatorText("OpenHoldem et fichiers");
    ImGui::SetNextItemWidth(em(520));
    ImGui::InputText("Dossier OpenHoldem", &ohRoot);
    ImGui::SameLine();
    if (ImGui::Button("Parcourir...##root")) { std::string d = platform_.pickFolder("Dossier du dépôt OpenHoldem (contenant OpenPPL)"); if (!d.empty()) ohRoot = d; }
    if (ohRootOk_) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1), "OK : bibliotheque OpenPPL trouvee."); 
    else ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Dossier invalide : il doit contenir OpenPPL/OpenPPL_Library (necessaire pour les profils OpenHoldem, pas pour les bots intégrés).");
    if (ImGui::Button("Enregistrer la configuration...")) {
        std::string p = platform_.saveFile("Enregistrer la configuration", "pokersim_config.json", "json");
        if (!p.empty() && !saveConfigFile(p)) error_ = "Impossible d'écrire " + p;
    }
    ImGui::SameLine();
    if (ImGui::Button("Charger une configuration...")) {
        std::string p = platform_.openFile("Charger une configuration", "Configuration PokerSim (*.json)", "*.json");
        if (!p.empty() && !loadConfigFile(p)) error_ = "Configuration illisible : " + p;
    }
    ImGui::EndChild();
    ImGui::EndDisabled();
}

void App::drawFormatSpecific() {
    if (cfg.format == ps::GameFormat::Cash) {
        ImGui::SeparatorText("Cash game");
        ImGui::SetNextItemWidth(em(260)); inputIntClamped("Petite blind", &cfg.cashSb, 1, 1000000, 1);
        ImGui::SetNextItemWidth(em(260)); inputIntClamped("Grosse blind", &cfg.cashBb, 1, 1000000, 1);
        ImGui::SetNextItemWidth(em(260)); inputIntClamped("Ante", &cfg.cashAnte, 0, 1000000, 1);
        ImGui::SetNextItemWidth(em(260)); inputIntClamped("Tapis (jetons)", &cfg.cashStack, 2, 100000000, 100);
        ImGui::SetNextItemWidth(em(260)); inputIntClamped("Mains par session", &cfg.cashHands, 1, 10000000, 100);
        static const char* modes[] = {"Recave si tapis a zero", "Tapis remis a chaque main"};
        int m = cfg.cashStackMode == ps::CashStackMode::Reset ? 1 : 0;
        ImGui::SetNextItemWidth(em(260));
        if (ImGui::Combo("Gestion des tapis", &m, modes, 2)) cfg.cashStackMode = m ? ps::CashStackMode::Reset : ps::CashStackMode::Rebuy;
        help("« Tapis remis » reproduit un jeu à profondeur constante (idéal pour comparer des stratégies).");
        ImGui::SetNextItemWidth(em(260)); inputDouble("Valeur d'un jeton", &cfg.chipValue, 0.0001, 1000.0, "%.4f");
        ImGui::SetNextItemWidth(em(260)); inputDouble("Rake (%)", &cfg.rakePct, 0.0, 10.0);
        ImGui::SetNextItemWidth(em(260)); inputIntClamped("Plafond du rake (jetons, 0 = aucun)", &cfg.rakeCap, 0, 100000000, 10);
        ImGui::Checkbox("Pas de rake sans flop (No Flop No Drop)", &cfg.noFlopNoDrop);
        return;
    }
    const bool spin = cfg.format == ps::GameFormat::SpinGo;
    ImGui::SeparatorText(spin ? "Spin & Go" : "Sit & Go");
    ImGui::SetNextItemWidth(em(260)); inputIntClamped("Tapis de départ", &cfg.startingStack, 10, 100000000, 100);
    ImGui::SetNextItemWidth(em(260)); inputDouble("Buy-in (frais compris)", &cfg.buyin, 0.01, 1000000.0);
    if (!spin) { ImGui::SetNextItemWidth(em(260)); inputDouble("Frais du buy-in (%)", &cfg.feePct, 0.0, 50.0); help("Part du prize pool retenue par la salle."); }
    ImGui::SetNextItemWidth(em(260)); inputIntClamped("Mains max par partie", &cfg.maxHandsPerGame, 10, 100000, 100);
    help("Sécurité : au-delà, la partie est arrêtée et classée au nombre de jetons.");

    ImGui::SeparatorText("Montee des blinds");
    static const char* by[] = {"Au temps simule", "Au nombre de mains"};
    int b = cfg.levelBy == ps::LevelBy::Hands ? 1 : 0;
    ImGui::SetNextItemWidth(em(260));
    if (ImGui::Combo("Les niveaux montent", &b, by, 2)) cfg.levelBy = b ? ps::LevelBy::Hands : ps::LevelBy::Time;
    if (cfg.levelBy == ps::LevelBy::Time) {
        ImGui::SetNextItemWidth(em(260)); inputIntClamped("Duree d'un niveau (secondes)", &cfg.levelSeconds, 5, 36000, 10);
        ImGui::SetNextItemWidth(em(260)); inputDouble("Duree d'une main (base, s)", &cfg.handBaseSeconds, 0.0, 600.0, "%.1f");
        ImGui::SetNextItemWidth(em(260)); inputDouble("Duree par action (s)", &cfg.actionSeconds, 0.0, 600.0, "%.1f");
        ImGui::SetNextItemWidth(em(260)); inputDouble("Duree du showdown (s)", &cfg.showdownSeconds, 0.0, 600.0, "%.1f");
        help("Le temps simulé d'une main = base + action x nombre d'actions + showdown. Il pilote la montée des blinds.");
    } else { ImGui::SetNextItemWidth(em(260)); inputIntClamped("Mains par niveau", &cfg.levelHands, 1, 100000, 1); }

    if (ImGui::Button("Structure Spin & Go par defaut")) cfg.levels = ps::defaultSpinLevels();
    ImGui::SameLine();
    if (ImGui::Button("Structure SNG par defaut")) cfg.levels = ps::defaultSngLevels();
    if (ImGui::BeginTable("levels", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit, ImVec2(em(520), em(0)))) {
        ImGui::TableSetupColumn("Niveau"); ImGui::TableSetupColumn("Petite blind"); ImGui::TableSetupColumn("Grosse blind"); ImGui::TableSetupColumn("Ante"); ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        int del = -1;
        for (size_t i = 0; i < cfg.levels.size(); ++i) {
            ps::BlindLevel& l = cfg.levels[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%zu", i + 1);
            ImGui::TableNextColumn(); ImGui::SetNextItemWidth(em(110)); ImGui::InputInt("##sb", &l.sb, 0, 0); l.sb = std::max(1, l.sb);
            ImGui::TableNextColumn(); ImGui::SetNextItemWidth(em(110)); ImGui::InputInt("##bb", &l.bb, 0, 0); l.bb = std::max(l.sb, l.bb);
            ImGui::TableNextColumn(); ImGui::SetNextItemWidth(em(110)); ImGui::InputInt("##ante", &l.ante, 0, 0); l.ante = std::max(0, l.ante);
            ImGui::TableNextColumn(); if (cfg.levels.size() > 1 && ImGui::SmallButton("Suppr.")) del = static_cast<int>(i);
            ImGui::PopID();
        }
        if (del >= 0) cfg.levels.erase(cfg.levels.begin() + del);
        ImGui::EndTable();
    }
    if (ImGui::Button("Ajouter un niveau") && !cfg.levels.empty()) {
        ps::BlindLevel l = cfg.levels.back();
        l.sb = std::max(l.sb + 1, l.sb * 3 / 2); l.bb = l.sb * 2;
        cfg.levels.push_back(l);
    }
    help("Après le dernier niveau, les blinds restent à ce niveau.");

    ImGui::SeparatorText("Repartition des gains");
    if (ImGui::Button("Gains par defaut")) cfg.payoutShares = ps::defaultPayoutShares(cfg.numPlayers);
    if (ImGui::BeginTable("payouts", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit, ImVec2(em(360), em(0)))) {
        ImGui::TableSetupColumn("Place"); ImGui::TableSetupColumn("Part du prize pool (%)"); ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        int del = -1;
        for (size_t i = 0; i < cfg.payoutShares.size(); ++i) {
            ImGui::PushID(static_cast<int>(i + 1000));
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%zu", i + 1);
            ImGui::TableNextColumn();
            double pct = cfg.payoutShares[i] * 100.0;
            ImGui::SetNextItemWidth(em(150));
            if (ImGui::InputDouble("##pct", &pct, 0.0, 0.0, "%.2f")) cfg.payoutShares[i] = std::clamp(pct, 0.0, 100.0) / 100.0;
            ImGui::TableNextColumn(); if (cfg.payoutShares.size() > 1 && ImGui::SmallButton("Suppr.")) del = static_cast<int>(i);
            ImGui::PopID();
        }
        if (del >= 0) cfg.payoutShares.erase(cfg.payoutShares.begin() + del);
        ImGui::EndTable();
    }
    if (static_cast<int>(cfg.payoutShares.size()) < cfg.numPlayers && ImGui::Button("Ajouter une place payee")) cfg.payoutShares.push_back(0.0);
    double sum = 0;
    for (double x : cfg.payoutShares) sum += x;
    if (std::fabs(sum - 1.0) > 1e-6) ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "Total : %.2f %% (doit faire 100 %%)", sum * 100.0);
    else ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1), "Total : 100 %%");

    if (spin) {
        ImGui::SeparatorText("Multiplicateurs (cagnotte)");
        if (ImGui::Button("Multiplicateurs par defaut")) cfg.multipliers = ps::defaultSpinMultipliers();
        long long totalW = 0;
        for (auto& m : cfg.multipliers) totalW += m.second;
        double ev = 0;
        for (auto& m : cfg.multipliers) ev += m.first * static_cast<double>(m.second);
        if (totalW > 0) ev /= static_cast<double>(totalW);
        if (ImGui::BeginTable("mult", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit, ImVec2(em(520), em(0)))) {
            ImGui::TableSetupColumn("Multiplicateur"); ImGui::TableSetupColumn("Poids"); ImGui::TableSetupColumn("Probabilite"); ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();
            int del = -1;
            for (size_t i = 0; i < cfg.multipliers.size(); ++i) {
                ImGui::PushID(static_cast<int>(i + 2000));
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(em(140)); ImGui::InputDouble("##m", &cfg.multipliers[i].first, 0.0, 0.0, "%.1f"); cfg.multipliers[i].first = std::max(0.0, cfg.multipliers[i].first);
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(em(140)); ImGui::InputInt("##w", &cfg.multipliers[i].second, 0, 0); cfg.multipliers[i].second = std::max(0, cfg.multipliers[i].second);
                ImGui::TableNextColumn(); ImGui::Text("%.4f %%", totalW > 0 ? 100.0 * cfg.multipliers[i].second / static_cast<double>(totalW) : 0.0);
                ImGui::TableNextColumn(); if (cfg.multipliers.size() > 1 && ImGui::SmallButton("Suppr.")) del = static_cast<int>(i);
                ImGui::PopID();
            }
            if (del >= 0) cfg.multipliers.erase(cfg.multipliers.begin() + del);
            ImGui::EndTable();
        }
        if (ImGui::Button("Ajouter un multiplicateur")) cfg.multipliers.push_back({2.0, 1000});
        ImGui::Checkbox("Cagnotte = multiplicateur x buy-in x nombre de joueurs", &cfg.multiplierTimesTotal);
        help("Décoché : cagnotte = multiplicateur x buy-in (cas d'un Spin & Go classique).");
        const double rtp = (cfg.multiplierTimesTotal ? ev : ev / std::max(1, cfg.numPlayers)) * 100.0;
        ImGui::Text("Multiplicateur moyen : %.4f   -   Retour theorique : %.2f %%", ev, rtp);
    }
}

// ============================================================ joueurs
void App::drawPlayersTab() {
    if (static_cast<int>(players.size()) < cfg.numPlayers) players.resize(static_cast<size_t>(cfg.numPlayers));
    ImGui::TextWrapped("Chaque joueur est soit un bot intégré (pour tester), soit un profil OpenHoldem (.ohf OpenPPL / OH-script, .oppl, ou .txt Shanky ancien format).");
    ImGui::BeginDisabled(running_);
    if (ImGui::BeginTable("players", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, em(30));
        ImGui::TableSetupColumn("Nom", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Fichier de profil", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Diagnostic", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableHeadersRow();
        static const char* kinds[] = {"Bot integre : tight-agressif", "Bot integre : call station", "Bot integre : aleatoire", "Profil OpenHoldem (fichier)"};
        for (int i = 0; i < cfg.numPlayers; ++i) {
            PlayerRow& p = players[static_cast<size_t>(i)];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d", i + 1);
            ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-em(1)); ImGui::InputText("##name", &p.name);
            ImGui::TableNextColumn();
            int k = static_cast<int>(p.kind);
            ImGui::SetNextItemWidth(-em(1));
            if (ImGui::Combo("##kind", &k, kinds, 4)) p.kind = static_cast<PlayerKind>(k);
            ImGui::TableNextColumn();
            if (p.kind == PlayerKind::ProfileFile) {
                ImGui::SetNextItemWidth(-em(60));
                ImGui::InputText("##path", &p.path);
                ImGui::SameLine();
                if (ImGui::Button("...")) {
                    std::string f = platform_.openFile("Profil OpenHoldem", "Profils OpenHoldem (*.ohf;*.oppl;*.txt)", "*.ohf;*.oppl;*.txt");
                    if (!f.empty()) { p.path = f; if (p.name.rfind("Joueur ", 0) == 0) { size_t s = f.find_last_of("/\\"), d = f.find_last_of('.'); p.name = f.substr(s == std::string::npos ? 0 : s + 1, d == std::string::npos || (s != std::string::npos && d < s) ? std::string::npos : d - (s == std::string::npos ? 0 : s + 1)); } }
                }
            } else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (p.kind == PlayerKind::ProfileFile) {
                auto it = reports_.find(p.path);
                if (ImGui::SmallButton("Verifier")) { reports_.erase(p.path); reportPath_ = p.path; report(p.path); }
                if (it != reports_.end()) {
                    ImGui::SameLine();
                    if (it->second.ok()) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1), "OK");
                    else if (!it->second.error.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "Illisible");
                    else ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "%zu erreur(s)", it->second.problems.size());
                }
            } else ImGui::TextDisabled("-");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Reinitialiser les joueurs")) resetPlayers();
    ImGui::EndDisabled();

    if (!reportPath_.empty() && reports_.count(reportPath_)) {
        const ps::oh::ProfileReport& r = reports_[reportPath_];
        ImGui::SeparatorText("Diagnostic du profil");
        ImGui::TextWrapped("%s", r.path.c_str());
        if (!r.error.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "%s", r.error.c_str());
        else {
            ImGui::Text("Format : %s", r.format.c_str());
            ImGui::Text("%d fonctions, %d listes de mains", r.functions, r.lists);
            if (r.problems.empty()) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1), "Aucune erreur de syntaxe.");
            else if (ImGui::BeginTable("problems", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(em(0), em(220)))) {
                ImGui::TableSetupColumn("Fonction", ImGuiTableColumnFlags_WidthFixed, em(220)); ImGui::TableSetupColumn("Message");
                ImGui::TableHeadersRow();
                for (auto& pr : r.problems) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(pr.first.c_str()); ImGui::TableNextColumn(); ImGui::TextWrapped("%s", pr.second.c_str()); }
                ImGui::EndTable();
            }
        }
    }
}

// ============================================================ résultats
void App::drawResultsTab() {
    if (!results_) { ImGui::TextWrapped("Aucun résultat pour l'instant. Configurez la partie, choisissez les joueurs, puis cliquez sur « Lancer la simulation »."); return; }
    const ResultsView& r = *results_;
    const bool cash = r.cfg.isCash();
    ImGui::Text("%s - %d joueurs - %zu %s - %lld mains - %.1f s%s", ps::formatName(r.cfg.format), r.cfg.numPlayers, r.summary.games.size(), cash ? "sessions" : "parties",
                r.totalHands, r.seconds, r.cancelled ? " - INTERROMPU (résultats partiels)" : "");
    if (ImGui::Button("Copier le resume")) ImGui::SetClipboardText(summaryText().c_str());
    ImGui::SameLine();
    if (ImGui::Button("Exporter en CSV...")) {
        std::string p = platform_.saveFile("Exporter les résultats", "resultats.csv", "csv");
        if (!p.empty() && !exportCsv(p)) error_ = "Impossible d'écrire " + p;
    }
    ImGui::BeginChild("res", ImVec2(em(0), em(0)), ImGuiChildFlags_None);
    const int cols = cash ? 7 : 8;
    if (ImGui::BeginTable("stats", cols, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Joueur", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        if (cash) {
            ImGui::TableSetupColumn("Sessions"); ImGui::TableSetupColumn("Mains"); ImGui::TableSetupColumn("Net (jetons)"); ImGui::TableSetupColumn("Gain");
            ImGui::TableSetupColumn("bb/100 (+/- IC 95%)", ImGuiTableColumnFlags_WidthStretch, 1.8f); ImGui::TableSetupColumn("Recaves");
        } else {
            ImGui::TableSetupColumn("Parties"); ImGui::TableSetupColumn("Victoires"); ImGui::TableSetupColumn("2e place"); ImGui::TableSetupColumn("3e place");
            ImGui::TableSetupColumn("Place moy."); ImGui::TableSetupColumn("Net"); ImGui::TableSetupColumn("ROI % (+/- IC 95%)", ImGuiTableColumnFlags_WidthStretch, 1.8f);
        }
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < r.players.size(); ++i) {
            const PlayerView& p = r.players[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(palette(static_cast<int>(i))));
            ImGui::TextUnformatted(p.name.c_str());
            ImGui::PopStyleColor();
            if (cash) {
                ImGui::TableNextColumn(); ImGui::Text("%d", p.games);
                ImGui::TableNextColumn(); ImGui::Text("%lld", p.hands);
                ImGui::TableNextColumn(); ImGui::Text("%lld", p.netChips);
                ImGui::TableNextColumn(); ImGui::Text("%.2f", p.net);
                ImGui::TableNextColumn(); ImGui::Text("%.2f +/- %.2f", p.bb100, p.bb100Ci);
                ImGui::TableNextColumn(); ImGui::Text("%d", p.rebuys);
            } else {
                ImGui::TableNextColumn(); ImGui::Text("%d", p.games);
                for (int pl = 0; pl < 3; ++pl) { ImGui::TableNextColumn(); if (pl < r.cfg.numPlayers && p.games) ImGui::Text("%d (%.1f%%)", p.placeCount[pl], 100.0 * p.placeCount[pl] / p.games); else ImGui::TextDisabled("-"); }
                ImGui::TableNextColumn(); ImGui::Text("%.3f", p.avgPlace);
                ImGui::TableNextColumn(); ImGui::Text("%.2f", p.net);
                ImGui::TableNextColumn(); ImGui::Text("%.2f +/- %.2f", p.roi, p.roiCi);
            }
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("IC 95%% : si l'intervalle d'un joueur chevauche celui d'un autre, la différence n'est pas statistiquement établie. Augmentez le nombre de parties.");

    std::vector<std::string> labels; std::vector<double> vals, errs;
    for (auto& p : r.players) { labels.push_back(p.name); vals.push_back(cash ? p.bb100 : p.roi); errs.push_back(cash ? p.bb100Ci : p.roiCi); }
    ImGui::SeparatorText(cash ? "bb/100 par joueur" : "ROI (%) par joueur");
    drawBars("##bars", labels, vals, errs, cash ? "bb/100" : "ROI %", em(220));
    ImGui::SeparatorText("Gain cumule");
    drawLines("##lines", r.cumulative, labels, em(260));

    if (!r.unknownSymbols.empty()) {
        ImGui::SeparatorText("Symboles inconnus rencontres (evalues a 0)");
        ImGui::TextWrapped("Absents d'OpenHoldem et de sa bibliothèque : symbole Shanky non converti, ancien symbole WinHoldem ou faute de frappe du profil.");
        if (ImGui::BeginTable("unk", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(em(0), em(200)))) {
            ImGui::TableSetupColumn("Symbole"); ImGui::TableSetupColumn("Evaluations", ImGuiTableColumnFlags_WidthFixed, em(120));
            ImGui::TableHeadersRow();
            for (auto& u : r.unknownSymbols) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(u.second.c_str()); ImGui::TableNextColumn(); ImGui::Text("%lld", u.first); }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

// ============================================================ historique
void App::drawHandsTab() {
    if (!results_) { ImGui::TextWrapped("Lancez une simulation avec « Enregistrer les N premières parties » > 0 (onglet Configuration)."); return; }
    std::vector<const ps::GameResult*> rec;
    for (auto& g : results_->summary.games) if (!g.records.empty()) rec.push_back(&g);
    if (rec.empty()) { ImGui::TextWrapped("Aucun historique enregistré pour cette simulation. Réglez « Enregistrer les N premières parties » (onglet Configuration) puis relancez."); return; }
    handGame = std::clamp(handGame, 0, static_cast<int>(rec.size()) - 1);
    const ps::GameResult& g = *rec[static_cast<size_t>(handGame)];
    if (rec.size() > 1) { ImGui::SetNextItemWidth(em(200)); ImGui::SliderInt("Partie", &handGame, 0, static_cast<int>(rec.size()) - 1); }
    const int n = static_cast<int>(g.records.size());
    handIndex = std::clamp(handIndex, 0, n - 1);
    if (ImGui::Button("<") && handIndex > 0) --handIndex;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em(400));
    ImGui::SliderInt("Main", &handIndex, 0, n - 1, "%d");
    ImGui::SameLine();
    if (ImGui::Button(">") && handIndex < n - 1) ++handIndex;
    ImGui::SameLine();
    ImGui::Text("(%d mains dans cette partie)", n);
    std::vector<std::string> names;
    for (auto& p : g.players) names.push_back(p.name);
    std::string text = ps::formatHandHistory(g.records[static_cast<size_t>(handIndex)], names, g.format);
    if (ImGui::Button("Copier")) ImGui::SetClipboardText(text.c_str());
    ImGui::InputTextMultiline("##hand", &text, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
}

}  // namespace psgui
