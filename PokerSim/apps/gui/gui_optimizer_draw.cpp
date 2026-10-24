// Onglet « Optimisation » : dessin Dear ImGui. La logique est dans gui_optimizer.cpp (testée sans écran).
// Textes en ASCII / Latin-1 uniquement (police ImGui par défaut).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include "gui_app.hpp"
#include "imgui.h"
#include "misc/imgui_stdlib.h"
#include "pokersim/pathutil.hpp"

namespace psgui {
namespace {

float em(float px) { return px * ImGui::GetFontSize() / 17.0f; }

void help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f); ImGui::TextUnformatted(text); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
}

bool inInt(const char* label, int* v, int lo, int hi, int step = 1) {
    ImGui::SetNextItemWidth(em(150));
    const bool ch = ImGui::InputInt(label, v, step, step * 10);
    *v = std::clamp(*v, lo, hi);
    return ch;
}
bool inDouble(const char* label, double* v, double lo, double hi, const char* fmt = "%.2f") {
    ImGui::SetNextItemWidth(em(150));
    const bool ch = ImGui::InputDouble(label, v, 0.0, 0.0, fmt);
    *v = std::clamp(*v, lo, hi);
    return ch;
}

// Ligne de chemin : étiquette (colonne fixe) | champ (largeur restante) | bouton « ... » (dossier ou fichier). Tient dans toute largeur.
void pathRow(Platform& plat, const char* label, const char* id, std::string* path, bool folder, const char* title, const char* tip = nullptr) {
    ImGui::PushID(id);
    const float labelW = em(235), btnW = em(36);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (tip) help(tip);
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(std::max(em(60), ImGui::GetContentRegionAvail().x - btnW - ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputText("##path", path);
    ImGui::SameLine();
    if (ImGui::Button("...", ImVec2(btnW, 0))) {
        const std::string r = folder ? plat.pickFolder(title) : plat.openFile(title, "Programmes", "*.exe");
        if (!r.empty()) *path = r;
    }
    ImGui::PopID();
}

const char* taskName(OptTask t) {
    switch (t) {
    case OptTask::Check: return "Verification de l'environnement";
    case OptTask::Pool: return "Creation du pool de bots";
    case OptTask::Optimize: return "Optimisation";
    case OptTask::Validate: return "Validation";
    default: return "";
    }
}

// Courbe : meilleur bb/100 (avec IC 95 %) par génération
void drawProgress(const std::vector<HistPoint>& h, float height) {
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = std::max(80.0f, ImGui::GetContentRegionAvail().x);
    ImGui::InvisibleButton("##curve", ImVec2(w, height));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + height), IM_COL32(25, 25, 30, 255));
    if (h.empty()) {
        dl->AddText(ImVec2(p0.x + 10, p0.y + height / 2 - 8), IM_COL32(150, 150, 150, 255), "Pas encore de generation terminee.");
        return;
    }
    double lo = 0, hi = 0;
    for (const auto& p : h) { lo = std::min(lo, p.mean - p.ci); hi = std::max(hi, p.mean + p.ci); }
    if (hi - lo < 1e-9) hi = lo + 1;
    const double pad = (hi - lo) * 0.1;
    lo -= pad; hi += pad;
    const float left = p0.x + 48, right = p0.x + w - 12, top = p0.y + 14, bottom = p0.y + height - 22;
    auto yOf = [&](double v) { return static_cast<float>(bottom - (v - lo) / (hi - lo) * (bottom - top)); };
    const int n = static_cast<int>(h.size());
    const int g0 = h.front().gen, g1 = std::max(h.back().gen, g0 + 1);
    auto xOf = [&](int g) { return left + static_cast<float>(g - g0) / static_cast<float>(g1 - g0) * (right - left); };
    const float y0 = yOf(0.0);
    dl->AddLine(ImVec2(left, y0), ImVec2(right, y0), IM_COL32(120, 120, 120, 255));
    char buf[64];
    std::snprintf(buf, sizeof buf, "%+.0f", hi); dl->AddText(ImVec2(p0.x + 4, top - 6), IM_COL32(170, 170, 170, 255), buf);
    std::snprintf(buf, sizeof buf, "%+.0f", lo); dl->AddText(ImVec2(p0.x + 4, bottom - 8), IM_COL32(170, 170, 170, 255), buf);
    dl->AddText(ImVec2(left, p0.y + height - 18), IM_COL32(170, 170, 170, 255), "generation");
    dl->AddText(ImVec2(p0.x + 4, p0.y + 1), IM_COL32(170, 170, 170, 255), "bb/100 (meilleur profil, IC 95 %)");
    for (int i = 1; i < n; ++i) if (h[i].stage != h[i - 1].stage) {
        const float x = (xOf(h[i - 1].gen) + xOf(h[i].gen)) * 0.5f;
        dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), IM_COL32(150, 150, 150, 120), 1.0f);
        char sb[24]; std::snprintf(sb, sizeof sb, "etape %d", h[i].stage);
        dl->AddText(ImVec2(x + 3, top - 12), IM_COL32(170, 170, 170, 255), sb);
    }
    for (int i = 0; i < n; ++i) {
        const float x = xOf(h[i].gen);
        dl->AddLine(ImVec2(x, yOf(h[i].mean - h[i].ci)), ImVec2(x, yOf(h[i].mean + h[i].ci)), IM_COL32(86, 180, 233, 110), 2.0f);
    }
    for (int i = 1; i < n; ++i) dl->AddLine(ImVec2(xOf(h[i - 1].gen), yOf(h[i - 1].mean)), ImVec2(xOf(h[i].gen), yOf(h[i].mean)), IM_COL32(86, 180, 233, 255), 2.0f);
    for (int i = 0; i < n; ++i) dl->AddCircleFilled(ImVec2(xOf(h[i].gen), yOf(h[i].mean)), 3.5f, IM_COL32(230, 159, 0, 255));
    if (hovered) {
        const float mx = ImGui::GetIO().MousePos.x;
        int best = 0;
        for (int i = 1; i < n; ++i) if (std::fabs(xOf(h[i].gen) - mx) < std::fabs(xOf(h[best].gen) - mx)) best = i;
        ImGui::BeginTooltip();
        ImGui::Text("Generation %d : %+.1f +/- %.1f bb/100 (%lld mains)", h[best].gen, h[best].mean, h[best].ci, h[best].hands);
        ImGui::EndTooltip();
    }
}

}  // namespace

void App::drawOptimizerTab() {
    OptSettings& s = opt.s;
    const bool busy = opt.busy();
    const ImVec4 red(1.0f, 0.4f, 0.35f, 1.0f), green(0.4f, 0.9f, 0.5f, 1.0f), orange(1.0f, 0.7f, 0.3f, 1.0f);

    ImGui::BeginChild("opt", ImVec2(em(0), em(0)), ImGuiChildFlags_None);
    ImGui::TextWrapped("Cherche automatiquement un profil qui bat un pool de bots : chaque generation cree des variantes (ohpg), les fait jouer dans "
                       "ce simulateur, garde les meilleures. Le resultat est ensuite verifie sur des bots jamais vus pendant la recherche. "
                       "Necessite Python 3 et le depot OpenHoldem-Profile-Generator-WIP (dossier optimizer/).");

    // ---------------------------------------------------------------- environnement
    ImGui::SeparatorText("1. Environnement");
    ImGui::BeginDisabled(busy);
    pathRow(platform_, "Python", "py", &s.python, false, "Executable Python",
            "Nom ou chemin de Python 3 (python sous Windows, python3 sous Linux/macOS).");
    pathRow(platform_, "Dossier des scripts", "sc", &s.scriptsDir, true, "Dossier des scripts (optimizer)",
            "Le dossier optimizer/ du depot OpenHoldem-Profile-Generator-WIP (ou le depot poker-profile-optimizer) : il contient optimize.py.");
    pathRow(platform_, "Dossier du generateur (ohpg)", "oh", &s.ohpgDir, true, "Dossier OpenHoldem-Profile-Generator-WIP",
            "Le dossier OpenHoldem-Profile-Generator-WIP : il contient le dossier ohpg/.");
    pathRow(platform_, "pokersim (ligne de commande)", "ex", &s.pokersimExe, false, "pokersim",
            "Le pokersim de CETTE version (option --json). Il est compile a cote de l'interface.");
    ImGui::Text("Racine OpenHoldem : %s", ohRoot.empty() ? "(non definie : onglet Configuration)" : ohRoot.c_str());
    if (ImGui::Button("Verifier l'environnement")) { std::string e; if (!opt.start(OptTask::Check, false, ohRoot, e)) opt.lastError = e; else optNote_.clear(); }
    ImGui::EndDisabled();
    if (opt.envChecked) { ImGui::SameLine(); ImGui::TextColored(opt.envOk ? green : red, "%s", opt.envMessage.c_str()); }

    // ---------------------------------------------------------------- pool
    ImGui::SeparatorText("2. Pool d'adversaires");
    ImGui::BeginDisabled(busy);
    pathRow(platform_, "Dossier du pool", "pool", &s.poolDir, true, "Dossier du pool de bots");
    inInt("Graine du pool", &s.poolSeed, 0, 1000000000);
    help("Meme graine = meme pool. Le pool contient 15 bots (6 styles x 2 a 3 variantes) : 10 pour l'entrainement, 5 reserves a la validation.");
    ImGui::SameLine();
    if (ImGui::Button(opt.poolBots ? "Recreer le pool" : "Creer le pool")) { std::string e; if (!opt.start(OptTask::Pool, false, ohRoot, e)) opt.lastError = e; else optNote_.clear(); }
    ImGui::EndDisabled();
    if (opt.poolBots) ImGui::TextColored(green, "Pool pret : %d bots (%d entrainement, %d validation)", opt.poolBots, opt.poolTrain, opt.poolValidation);
    else ImGui::TextColored(orange, "Aucun pool dans ce dossier.");
    if (opt.poolBots && opt.stateExists) ImGui::TextDisabled("Recreer le pool change les adversaires : les resultats d'un run existant ne seront plus comparables.");

    // ---------------------------------------------------------------- optimisation
    ImGui::SeparatorText("3. Optimisation");
    ImGui::BeginDisabled(busy);
    pathRow(platform_, "Dossier du run", "run", &s.runDir, true, "Dossier de cette optimisation",
            "Contient l'etat (reprise possible), le meilleur profil (best.ohf), l'historique et la validation.");
    {
        static const char* modes[] = {"Rapide : 23 parametres de style", "Complet en une etape (87 parametres)",
                                      "Complet par etapes (recommande)", "Choisir les groupes..."};
        ImGui::SetNextItemWidth(em(330));
        ImGui::Combo("Parametres a optimiser", &s.searchMode, modes, 4);
        help("Par etapes : 1) ouvertures, defense, 4bet  2) flop, turn, river  3) tailles de mise et reglages globaux. "
             "Chaque etape part du meilleur profil de la precedente et ne touche que ses propres parametres.");
        if (s.searchMode == 3) {
            for (int i = 0; i < kOptGroupCount; ++i) {
                bool on = (s.customGroups >> i) & 1;
                char lab[96]; std::snprintf(lab, sizeof lab, "%s (~%d)##grp%d", kOptGroups[i].label, kOptGroups[i].count, i);
                if (ImGui::Checkbox(lab, &on)) s.customGroups = on ? (s.customGroups | (1 << i)) : (s.customGroups & ~(1 << i));
                help(kOptGroups[i].help);
            }
        }
        ImGui::TextDisabled("%d parametres%s, %d etape%s, %d generations au total", opt.paramCount(), s.searchMode == 2 ? " (au plus par etape)" : "",
                            opt.stageCount(), opt.stageCount() > 1 ? "s" : "", opt.totalGens());
    }
    if (ImGui::TreeNode("Reglages de l'optimisation")) {
        inInt("Generations (par etape)", &s.gens, 1, 100000, 5); help("Generations de chaque etape. Total = etapes x generations. Un run repris continue la ou il s'est arrete.");
        inInt("Survivants (mu)", &s.mu, 2, 200); help("Taille de la population conservee d'une generation a l'autre.");
        inInt("Enfants par generation", &s.lam, 1, 400);
        inInt("Enfants affines", &s.refine, 1, 400); help("Les meilleurs enfants de l'etape 1 recoivent un gros volume de mains (etape 2).");
        inInt("Tables etape 1", &s.t1, 1, 1000); inInt("Tables etape 2", &s.t2, 1, 10000);
        inInt("Sessions par table", &s.games, 1, 1000); help("Etape 2 : le double.");
        inInt("Mains par session", &s.hands, 50, 1000000, 100);
        inDouble("Prudence k", &s.k, 0.0, 10.0, "%.1f"); help("Score de selection = moyenne - k x erreur-type. 0 = ne pas se mefier de la chance.");
        inDouble("Mutation initiale (sigma)", &s.sigma, 0.001, 1.0, "%.3f"); help("Fraction de la plage d'un parametre ; decroit pendant le run.");
        inInt("Graine", &s.seed, 0, 1000000000);
        inInt("Simulations en parallele", &s.jobs, 0, 256); help("0 = nombre de coeurs.");
        inInt("Plafond prwin", &s.prwinMax, 0, 1000000, 1000); help("Les profils generes n'utilisent pas prwin : sans effet pour eux.");
        inDouble("Rake (%)", &s.rake, 0.0, 20.0, "%.1f"); help("0 = jeu a somme nulle : bb/100 > 0 veut dire que le profil bat le champ.");
        inInt("Plafond du rake (jetons)", &s.rakeCap, 0, 100000, 5); help("BB = 10 jetons : 30 = 3 BB. 0 = sans plafond.");
        ImGui::Checkbox("Exploiter les stats adversaires (use_stats)", &s.useStats);
        help("Ajoute 16 parametres d'exploitation (nit / fish / maniaque) : une etape de plus en mode par etapes, sinon ajoutes a la recherche. Les profils lisent alors les stats PokerTracker simulees.");
        ImGui::TreePop();
    }
    if (ImGui::Button("Lancer l'optimisation", ImVec2(em(190), em(28)))) { std::string e; if (!opt.start(OptTask::Optimize, false, ohRoot, e)) opt.lastError = e; else optNote_.clear(); }
    ImGui::SameLine();
    ImGui::BeginDisabled(!opt.stateExists);
    if (ImGui::Button("Reprendre", ImVec2(em(110), em(28)))) { std::string e; if (!opt.start(OptTask::Optimize, true, ohRoot, e)) opt.lastError = e; else optNote_.clear(); }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!busy || opt.stopping());
    if (ImGui::Button("Arreter", ImVec2(em(100), em(28)))) opt.stop();
    ImGui::EndDisabled();
    if (!opt.stateExists && !busy) { ImGui::SameLine(); ImGui::TextDisabled("Un nouveau lancement ecrase le run precedent de ce dossier."); }

    if (busy) {
        const int total = std::max(1, opt.totalGens());
        const float frac = std::clamp(static_cast<float>(std::max(0, opt.currentGen)) / static_cast<float>(total), 0.0f, 1.0f);
        char ov[200];
        const double el = opt.elapsedSeconds(), eta = opt.etaSeconds();
        if (opt.task() == OptTask::Optimize) {
            char st[96] = "";
            if (opt.curStages > 1) std::snprintf(st, sizeof st, "etape %d/%d (%s)  -  ", opt.curStage, opt.curStages, opt.activeLabel.c_str());
            if (eta >= 0) std::snprintf(ov, sizeof ov, "%sGeneration %d / %d  -  %.0f min ecoulees, reste ~%.0f min%s", st, std::max(0, opt.currentGen), total, el / 60, eta / 60, opt.stopping() ? "  -  arret..." : "");
            else std::snprintf(ov, sizeof ov, "%sGeneration %d / %d  -  %.0f s ecoulees%s", st, std::max(0, opt.currentGen), total, el, opt.stopping() ? "  -  arret..." : "");
            ImGui::ProgressBar(frac, ImVec2(-1, em(24)), ov);
        } else {
            std::snprintf(ov, sizeof ov, "%s en cours...  %.0f s%s", taskName(opt.task()), el, opt.stopping() ? "  -  arret..." : "");
            ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(-1, em(24)), ov);
        }
    }
    if (!opt.lastError.empty()) {
        ImGui::TextColored(red, "%s", opt.lastError.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Fermer##opterr")) opt.lastError.clear();
    }
    if (!optNote_.empty()) ImGui::TextColored(green, "%s", optNote_.c_str());

    // ---------------------------------------------------------------- progression
    ImGui::SeparatorText("Progression");
    if (opt.bestExists) {
        ImGui::Text("Meilleur profil (generation %d) : %+.1f +/- %.1f bb/100 sur %lld mains (pool d'entrainement)", std::max(0, opt.currentGen), opt.bestMean, opt.bestCi, opt.bestHands);
        ImGui::TextDisabled("Ce score est mesure sur les bots d'entrainement : il est optimiste. Seule la validation (etape 4) fait foi.");
    } else ImGui::TextDisabled("Aucun resultat pour l'instant.");
    drawProgress(opt.history, em(200));

    // ---------------------------------------------------------------- validation
    ImGui::SeparatorText("4. Validation (bots jamais vus)");
    ImGui::BeginDisabled(busy);
    if (ImGui::TreeNode("Reglages de la validation")) {
        inInt("Tables", &s.vTables, 1, 10000); inInt("Sessions par table", &s.vGames, 1, 1000); inInt("Mains par session", &s.vHands, 50, 1000000, 100);
        help("Plus il y a de mains, plus l'intervalle de confiance est etroit : visez au moins 1 a 2 millions de mains par profil pour ~ +/- 2 bb/100.");
        char tot[96];
        std::snprintf(tot, sizeof tot, "Soit %lld mains par profil et par pool.", static_cast<long long>(s.vTables) * s.vGames * s.vHands * 6 / 6);
        ImGui::TextDisabled("%s", tot);
        ImGui::TreePop();
    }
    ImGui::BeginDisabled(!opt.bestExists);
    if (ImGui::Button("Valider le meilleur profil", ImVec2(em(220), em(28)))) { std::string e; if (!opt.start(OptTask::Validate, false, ohRoot, e)) opt.lastError = e; else optNote_.clear(); }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (!opt.validation.empty()) {
        if (ImGui::BeginTable("val", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Profil", ImGuiTableColumnFlags_WidthStretch, 1.3f);
            ImGui::TableSetupColumn("Entrainement (bb/100, IC 95 %)", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableSetupColumn("Validation - bots jamais vus", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableHeadersRow();
            for (const ValRow& r : opt.validation) {
                const bool cand = r.name == "CANDIDAT";
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (cand) ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1), "%s", r.name.c_str()); else ImGui::TextUnformatted(r.name.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%+.1f +/- %.1f  (%lld mains)", r.trMean, r.trCi, r.trHands);
                ImGui::TableNextColumn(); ImGui::Text("%+.1f +/- %.1f  (%lld mains)", r.vaMean, r.vaCi, r.vaHands);
            }
            ImGui::EndTable();
        }
        const ValRow* cand = nullptr;
        for (const ValRow& r : opt.validation) if (r.name == "CANDIDAT") cand = &r;
        if (cand) {
            const double gap = cand->trMean - cand->vaMean;
            ImGui::Text("Ecart entrainement - validation : %+.1f bb/100", gap);
            help("Un grand ecart positif signifie que le profil s'est specialise sur les bots d'entrainement (surapprentissage).");
            const ValRow* ref = nullptr;
            for (const ValRow& r : opt.validation) if (r.name != "CANDIDAT" && (!ref || r.vaMean > ref->vaMean)) ref = &r;
            if (ref) {
                const double diff = cand->vaMean - ref->vaMean, ci = std::sqrt(cand->vaCi * cand->vaCi + ref->vaCi * ref->vaCi);
                if (diff > ci) ImGui::TextColored(green, "Meilleur que la meilleure reference (%s) : %+.1f +/- %.1f bb/100", ref->name.c_str(), diff, ci);
                else if (diff < -ci) ImGui::TextColored(red, "Moins bon que la meilleure reference (%s) : %+.1f +/- %.1f bb/100", ref->name.c_str(), diff, ci);
                else ImGui::TextColored(orange, "Pas de difference significative avec la meilleure reference (%s) : %+.1f +/- %.1f bb/100", ref->name.c_str(), diff, ci);
            }
        }
    } else ImGui::TextDisabled("Pas encore de validation pour ce run.");

    // ---------------------------------------------------------------- utilisation du résultat
    ImGui::SeparatorText("Meilleur profil");
    ImGui::BeginDisabled(!opt.bestExists || busy);
    if (ImGui::Button("Utiliser comme joueur 1 du simulateur")) {
        if (useProfileAsPlayer(opt.bestOhfPath(), "OPT_BEST")) optNote_ = "Le meilleur profil est le joueur 1 (onglet Joueurs). Choisissez les adversaires puis lancez la simulation.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Exporter best.ohf...")) {
        const std::string dst = platform_.saveFile("Enregistrer le profil", "OPT_BEST.ohf", "ohf");
        if (!dst.empty()) {
            std::error_code ec;
            std::filesystem::copy_file(ps::pathFromUtf8(opt.bestOhfPath()), ps::pathFromUtf8(dst), std::filesystem::copy_options::overwrite_existing, ec);
            optNote_ = ec ? "Export impossible : " + ec.message() : "Profil exporte : " + dst;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Ouvrir le dossier du run")) platform_.openPath(s.runDir);
    ImGui::EndDisabled();
    if (opt.bestExists) ImGui::TextDisabled("%s", opt.bestOhfPath().c_str());

    // ---------------------------------------------------------------- journal
    ImGui::SeparatorText("Journal");
    if (ImGui::BeginChild("optlog", ImVec2(em(0), em(180)), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const std::string& l : opt.log) ImGui::TextUnformatted(l.c_str());
        if (busy && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

}  // namespace psgui
