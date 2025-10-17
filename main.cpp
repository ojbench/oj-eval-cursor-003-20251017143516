#include <bits/stdc++.h>
using namespace std;

struct Submission {
    int time;
    char problem; // 'A'..'Z'
    string status; // Accepted, Wrong_Answer, Runtime_Error, Time_Limit_Exceed
};

struct FrozenInfo {
    int incorrectBeforeFreeze = 0; // wrong attempts before freeze for this problem
    int submissionsDuringFreeze = 0; // number of submissions after freeze for this problem
    bool hasFrozen = false; // whether this problem is frozen for the team
    // Store submissions during freeze in order to apply on scroll
    vector<Submission> frozenSubmissions;
};

struct ProblemState {
    bool solved = false;
    int wrongBeforeAccept = 0;
    int solveTime = 0; // time of first accept
    // For display
    int totalWrong = 0; // wrong attempts when unsolved and unfrozen
};

struct Team {
    string name;
    int solvedCount = 0;
    long long penalty = 0; // sum over solved problems (20 * wrongBeforeAccept + solveTime)
    // For tiebreak after penalty: vector of solve times sorted descending
    vector<int> solveTimesDesc;

    // per problem state when not frozen
    array<ProblemState,26> prob;

    // frozen per-problem info
    array<FrozenInfo,26> frozen;
};

struct ScoreboardEntry {
    string name;
    int solved;
    long long penalty;
    vector<int> solveTimesDesc; // for tie-break
};

struct Competition {
    bool started = false;
    bool frozen = false;
    int duration = 0;
    int problemCount = 0;
    // No separate flag needed; lastFlushed is updated on explicit FLUSH and at end of SCROLL

    // Teams
    unordered_map<string,int> teamIndex; // name -> index in teams
    vector<Team> teams;

    // Snapshot used for last FLUSH (for ranking and QUERY_RANKING baseline)
    vector<ScoreboardEntry> lastFlushed;

    // Utility to get or create team index before start
    bool addTeam(const string &name, string &outMsg) {
        if (started) {
            outMsg = "[Error]Add failed: competition has started.\n";
            return false;
        }
        if (teamIndex.count(name)) {
            outMsg = "[Error]Add failed: duplicated team name.\n";
            return false;
        }
        Team t; t.name = name;
        teams.push_back(t);
        teamIndex[name] = (int)teams.size() - 1;
        outMsg = "[Info]Add successfully.\n";
        return true;
    }

    bool start(int dur, int pc, string &outMsg) {
        if (started) {
            outMsg = "[Error]Start failed: competition has started.\n";
            return false;
        }
        started = true;
        duration = dur;
        problemCount = pc;
        outMsg = "[Info]Competition starts.\n";
        return true;
    }

    // Compare two teams according to rules using their current visible state (considering frozen mask)
    static bool compareTeams(const Team &a, const Team &b) {
        if (a.solvedCount != b.solvedCount) return a.solvedCount > b.solvedCount;
        if (a.penalty != b.penalty) return a.penalty < b.penalty;
        size_t n = max(a.solveTimesDesc.size(), b.solveTimesDesc.size());
        for (size_t i = 0; i < n; ++i) {
            int ta = (i < a.solveTimesDesc.size() ? a.solveTimesDesc[i] : 0);
            int tb = (i < b.solveTimesDesc.size() ? b.solveTimesDesc[i] : 0);
            if (ta != tb) return ta < tb; // smaller maximum solve time ranks higher
        }
        return a.name < b.name;
    }

    vector<int> getSortedIndicesByCurrentState() const {
        vector<int> idx(teams.size());
        iota(idx.begin(), idx.end(), 0);
        // build Score view states are already in Team fields
        sort(idx.begin(), idx.end(), [&](int i, int j){
            if (teams[i].solvedCount != teams[j].solvedCount) return teams[i].solvedCount > teams[j].solvedCount;
            if (teams[i].penalty != teams[j].penalty) return teams[i].penalty < teams[j].penalty;
            size_t n = max(teams[i].solveTimesDesc.size(), teams[j].solveTimesDesc.size());
            for (size_t k = 0; k < n; ++k) {
                int ti = (k < teams[i].solveTimesDesc.size() ? teams[i].solveTimesDesc[k] : 0);
                int tj = (k < teams[j].solveTimesDesc.size() ? teams[j].solveTimesDesc[k] : 0);
                if (ti != tj) return ti < tj;
            }
            return teams[i].name < teams[j].name;
        });
        return idx;
    }

    void recalcFromVisibleStatesAndFlush() {
        // lastFlushed becomes snapshot according to current visible data
        vector<int> order = getSortedIndicesByCurrentState();
        lastFlushed.clear();
        lastFlushed.reserve(order.size());
        for (int idx : order) {
            const Team &t = teams[idx];
            ScoreboardEntry e;
            e.name = t.name;
            e.solved = t.solvedCount;
            e.penalty = t.penalty;
            e.solveTimesDesc = t.solveTimesDesc;
            lastFlushed.push_back(move(e));
        }
    }

    void handleSubmit(const string &probName, const string &teamName, const string &status, int time) {
        int pi = probName[0] - 'A';
        int ti = teamIndex[teamName];
        Team &t = teams[ti];

        if (frozen) {
            FrozenInfo &fz = t.frozen[pi];
            // Determine if this problem should be frozen for this team: frozen for unsolved problems only
            if (!t.prob[pi].solved) {
                if (!fz.hasFrozen) {
                    fz.hasFrozen = true;
                    fz.incorrectBeforeFreeze = t.prob[pi].totalWrong;
                }
                fz.submissionsDuringFreeze += 1;
                fz.frozenSubmissions.push_back(Submission{time, probName[0], status});
            } else {
                // Solved before freeze: submissions after freeze do not freeze or change display
                // No effect per problem description
            }
            return;
        }

        // Not frozen: update live state and visible fields
        ProblemState &ps = t.prob[pi];
        if (ps.solved) {
            // Further submissions do not affect anything
            return;
        }
        if (status == "Accepted") {
            ps.solved = true;
            ps.solveTime = time;
            t.solvedCount += 1;
            t.penalty += 20LL * ps.wrongBeforeAccept + time;
            // maintain descending solve times vector
            t.solveTimesDesc.push_back(time);
            sort(t.solveTimesDesc.begin(), t.solveTimesDesc.end(), greater<int>());
        } else {
            ps.wrongBeforeAccept += 1;
            ps.totalWrong += 1;
        }
    }

    void cmdFlush() {
        cout << "[Info]Flush scoreboard.\n";
        recalcFromVisibleStatesAndFlush();
        const char* dbg = getenv("DEBUG_PRINT_ON_FLUSH");
        if (dbg && string(dbg) == string("1")) {
            printScoreboardCurrentView();
        }
    }

    void cmdFreeze() {
        if (frozen) {
            cout << "[Error]Freeze failed: scoreboard has been frozen.\n";
            return;
        }
        frozen = true;
        cout << "[Info]Freeze scoreboard.\n";
    }

    void applyFrozenForTeamProblem(Team &t, int pi) {
        FrozenInfo &fz = t.frozen[pi];
        if (!fz.hasFrozen) return;
        // Apply all frozen submissions in order to the team's true state
        bool wasSolved = t.prob[pi].solved;
        for (const Submission &s : fz.frozenSubmissions) {
            if (t.prob[pi].solved) {
                // once solved, further submissions have no effect
                continue;
            }
            if (s.status == "Accepted") {
                t.prob[pi].solved = true;
                t.prob[pi].solveTime = s.time;
                t.solvedCount += 1;
                t.penalty += 20LL * t.prob[pi].wrongBeforeAccept + s.time;
                t.solveTimesDesc.push_back(s.time);
                sort(t.solveTimesDesc.begin(), t.solveTimesDesc.end(), greater<int>());
            } else {
                t.prob[pi].wrongBeforeAccept += 1;
                t.prob[pi].totalWrong += 1; // reflect wrongs after unfreeze in visible '-' counts
            }
        }
        // Clear frozen state for this problem after applying
        fz.hasFrozen = false;
        fz.incorrectBeforeFreeze = 0;
        fz.submissionsDuringFreeze = 0;
        fz.frozenSubmissions.clear();
        // After solving, ps.totalWrong is no longer used for display; keep as-is
        (void)wasSolved;
    }

    void printScoreboardCurrentView() {
        // Build ranking on current visible state
        vector<int> order = getSortedIndicesByCurrentState();
        // Build ranking positions (1-based)
        unordered_map<string,int> pos;
        pos.reserve(order.size()*2);
        for (size_t i = 0; i < order.size(); ++i) pos[teams[order[i]].name] = (int)i + 1;

        for (int idx : order) {
            Team &t = teams[idx];
            cout << t.name << ' ' << pos[t.name] << ' ' << t.solvedCount << ' ' << t.penalty;
            for (int p = 0; p < problemCount; ++p) {
                const ProblemState &ps = t.prob[p];
                const FrozenInfo &fz = t.frozen[p];
                cout << ' ';
                if (!fz.hasFrozen) {
                    if (ps.solved) {
                        if (ps.wrongBeforeAccept == 0) cout << '+';
                        else cout << '+' << ps.wrongBeforeAccept;
                    } else {
                        if (ps.totalWrong == 0) cout << '.';
                        else cout << '-' << ps.totalWrong;
                    }
                } else {
                    int x = fz.incorrectBeforeFreeze;
                    int y = fz.submissionsDuringFreeze;
                    if (x == 0) cout << '0' << '/' << y;
                    else cout << '-' << x << '/' << y;
                }
            }
            cout << "\n";
        }
    }

    void cmdScroll() {
        if (!frozen) {
            cout << "[Error]Scroll failed: scoreboard has not been frozen.\n";
            return;
        }
        cout << "[Info]Scroll scoreboard.\n";
        // Before scrolling, perform an implicit flush to set lastFlushed snapshot,
        // then output the scoreboard (which reflects the flushed state)
        recalcFromVisibleStatesAndFlush();
        // But freezing hides some info; visible state is already represented in Team fields and FrozenInfo
        // To follow sample: they first print scoreboard before scrolling (after flushing)
        // We need to flush visible ranking snapshot, but it should not modify frozen internals
        // We'll recalc rankings on visible state and print it
        // Determine current visible ranking
        printScoreboardCurrentView();

        // Now perform scrolling: repeatedly select lowest-ranked team with any frozen problems
        // At each step, unfreeze the smallest problem letter for that team, and if ranking changes, output change line.

        // Build helper: get current order function for dynamic changes
        auto getOrder = [&]() {
            return getSortedIndicesByCurrentState();
        };

        // Function to find lowest-ranked team having frozen problems
        auto hasFrozenAny = [&](const Team &t) {
            for (int i = 0; i < problemCount; ++i) if (t.frozen[i].hasFrozen) return true;
            return false;
        };

        while (true) {
            vector<int> order = getOrder();
            int candidate = -1;
            for (int i = (int)order.size() - 1; i >= 0; --i) {
                if (hasFrozenAny(teams[order[i]])) { candidate = order[i]; break; }
            }
            if (candidate == -1) break; // no more frozen problems

            // previous ranking position mapping
            unordered_map<string,int> prePos;
            for (size_t i = 0; i < order.size(); ++i) prePos[teams[order[i]].name] = (int)i + 1;

            // pick smallest problem index with frozen
            int pi = -1;
            for (int i = 0; i < problemCount; ++i) if (teams[candidate].frozen[i].hasFrozen) { pi = i; break; }
            if (pi == -1) break;

            // Unfreeze this problem for candidate: first, as per spec, scroll operation first flushes scoreboard before proceeding
            // So rankings should be recalculated before applying change. Our order above is already the current view.
            // Now apply the frozen submissions for that problem to team's true state
            applyFrozenForTeamProblem(teams[candidate], pi);

            // After applying, compute new ranking and if candidate's ranking increases, output change line, and repeat process on newly updated ranking
            vector<int> newOrder = getOrder();
            unordered_map<string,int> postPos;
            for (size_t i = 0; i < newOrder.size(); ++i) postPos[teams[newOrder[i]].name] = (int)i + 1;

            int pre = prePos[teams[candidate].name];
            int post = postPos[teams[candidate].name];
            if (post < pre) {
                // ranking improved. team_name1 is the improving team, team_name2 is the team previously at the position just before team1 after improvement
                // From spec: team_name2 represents the team that was at the position before team_name1's ranking increase (i.e., the team that was at the position before team_name1 moved up). Based on sample, they print the team displaced directly above.
                // Identify the team that currently occupies the position post (after improvement) other than candidate; equivalently, the one that was at position post before
                // We can find the team at position post in pre-order
                string displaced;
                for (auto &kv : prePos) {
                    if (kv.second == post) { displaced = kv.first; break; }
                }
                cout << teams[candidate].name << ' ' << displaced << ' ' << teams[candidate].solvedCount << ' ' << teams[candidate].penalty << "\n";
            }
        }

        // After scrolling ends, output the scoreboard after scrolling
        printScoreboardCurrentView();
        // Lift frozen state
        frozen = false;
        // Update lastFlushed snapshot to the final (post-scroll) standings
        recalcFromVisibleStatesAndFlush();
    }

    void cmdQueryRanking(const string &teamName) {
        auto it = teamIndex.find(teamName);
        if (it == teamIndex.end()) {
            cout << "[Error]Query ranking failed: cannot find the team.\n";
            return;
        }
        cout << "[Info]Complete query ranking.\n";
        if (frozen) {
            cout << "[Warning]Scoreboard is frozen. The ranking may be inaccurate until it were scrolled.\n";
        }
        // Ranking baseline:
        // - Before first explicit FLUSH: lexicographic order
        // - After explicit FLUSH: lastFlushed
        // - After SCROLL: lastFlushed updated to post-scroll standings
        int rank = 0;
        if (lastFlushed.empty()) {
            vector<string> names;
            names.reserve(teams.size());
            for (auto &t : teams) names.push_back(t.name);
            sort(names.begin(), names.end());
            string target = teamName;
            for (size_t i = 0; i < names.size(); ++i) if (names[i] == target) { rank = (int)i + 1; break; }
        } else {
            for (size_t i = 0; i < lastFlushed.size(); ++i) if (lastFlushed[i].name == teamName) { rank = (int)i + 1; break; }
        }
        cout << teamName << " NOW AT RANKING " << rank << "\n";
    }

    void cmdQuerySubmission(const string &teamName, const string &problemSel, const string &statusSel) {
        auto it = teamIndex.find(teamName);
        if (it == teamIndex.end()) {
            cout << "[Error]Query submission failed: cannot find the team.\n";
            return;
        }
        cout << "[Info]Complete query submission.\n";
        // We must search both live history and frozen submissions; but we did not store full history.
        // According to spec, input is guaranteed valid and we only need the last submission that satisfies condition and submissions after freezing can be queried.
        // To support this, we need to store all submissions globally. We'll maintain a per-team vector of all submissions.
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Competition comp;

    // We also need to record full submission history for queries
    // We'll store as: map team -> vector of Submission in chronological order
    unordered_map<string, vector<Submission>> submissionHistory;

    string cmd;
    while (true) {
        if (!(cin >> cmd)) break;
        if (cmd == "ADDTEAM") {
            string name; cin >> name;
            string msg; comp.addTeam(name, msg);
            cout << msg;
        } else if (cmd == "START") {
            string tmp; // DURATION
            int duration; string tmp2; int probCnt;
            cin >> tmp >> duration >> tmp2 >> probCnt;
            string msg; comp.start(duration, probCnt, msg);
            cout << msg;
        } else if (cmd == "SUBMIT") {
            string probName; string by; string teamName; string with; string status; string at; int time;
            cin >> probName >> by >> teamName >> with >> status >> at >> time;
            comp.handleSubmit(probName, teamName, status, time);
            // record history
            submissionHistory[teamName].push_back(Submission{time, probName[0], status});
        } else if (cmd == "FLUSH") {
            comp.cmdFlush();
        } else if (cmd == "FREEZE") {
            comp.cmdFreeze();
        } else if (cmd == "SCROLL") {
            comp.cmdScroll();
        } else if (cmd == "QUERY_RANKING") {
            string teamName; cin >> teamName;
            comp.cmdQueryRanking(teamName);
        } else if (cmd == "QUERY_SUBMISSION") {
            string teamName; string where; string probToken; string AND; string statusToken;
            cin >> teamName >> where >> probToken >> AND >> statusToken;
            // probToken is like "PROBLEM=A"; statusToken is like "STATUS=Accepted"
            auto getValueAfterEqual = [](const string &token) -> string {
                auto pos = token.find('=');
                if (pos == string::npos) return string();
                return token.substr(pos + 1);
            };
            string problemSel = getValueAfterEqual(probToken);
            string statusSel = getValueAfterEqual(statusToken);

            // Validate team existence
            if (!comp.teamIndex.count(teamName)) {
                cout << "[Error]Query submission failed: cannot find the team.\n";
                continue;
            }
            cout << "[Info]Complete query submission.\n";
            const auto &hist = submissionHistory[teamName];
            bool found = false;
            for (int i = (int)hist.size() - 1; i >= 0; --i) {
                const Submission &s = hist[i];
                bool okProblem = (problemSel == "ALL" || (problemSel.size()==1 && s.problem == problemSel[0]));
                bool okStatus = (statusSel == "ALL" || s.status == statusSel);
                if (okProblem && okStatus) {
                    cout << teamName << ' ' << s.problem << ' ' << s.status << ' ' << s.time << "\n";
                    found = true;
                    break;
                }
            }
            if (!found) {
                cout << "Cannot find any submission.\n";
            }
        } else if (cmd == "END") {
            cout << "[Info]Competition ends.\n";
            break;
        }
    }
    return 0;
}
