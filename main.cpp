#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <regex>
#include <curl/curl.h>

using namespace std;

mutex mtx;
int completed = 0;

vector<string> loadURLs(const string& filename) {
    vector<string> urls;
    ifstream file(filename);
    if (!file) {
        lock_guard<mutex> lock(mtx);
        cout << "Error opening file: " << filename << "\n";
        return urls;
    }
    string url;
    regex urlRegex(R"(https?://[a-zA-Z0-9\-\.]+\.[a-zA-Z]{2,}(/[^\s]*)?)");
    while (getline(file, url)) {
        if (regex_match(url, urlRegex)) {
            urls.push_back(url);
        } else {
            lock_guard<mutex> lock(mtx);
            cout << "Invalid URL skipped: " << url << "\n";
        }
    }
    return urls;
}

void downloadPage(const string& url, const string& filename, size_t total_urls) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        lock_guard<mutex> lock(mtx);
        cout << "Error initializing CURL for " << url << "\n";
        return;
    }

    FILE* file = fopen(filename.c_str(), "w");
    if (!file) {
        lock_guard<mutex> lock(mtx);
        cout << "Error opening file: " << filename << "\n";
        curl_easy_cleanup(curl);
        return;
    }

    // RAII for file and curl cleanup
    struct Cleanup {
        FILE* file;
        CURL* curl;
        ~Cleanup() { fclose(file); curl_easy_cleanup(curl); }
    } cleanup{file, curl};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L); // 30-second timeout
    CURLcode res = curl_easy_perform(curl);

    lock_guard<mutex> lock(mtx);
    if (res != CURLE_OK) {
        cout << "Download failed for " << url << ": " << curl_easy_strerror(res) << "\n";
    } else {
        completed++;
        cout << "Downloaded " << completed << "/" << total_urls << ": " << url << "\n";
    }
}

void worker(queue<pair<string, string>>& tasks, size_t total_urls) {
    while (true) {
        pair<string, string> task;
        {
            lock_guard<mutex> lock(mtx);
            if (tasks.empty()) return;
            task = tasks.front();
            tasks.pop();
        }
        downloadPage(task.first, task.second, total_urls);
    }
}

void downloadAll(const vector<string>& urls) {
    const int NUM_THREADS = min(4, static_cast<int>(thread::hardware_concurrency()));
    queue<pair<string, string>> tasks;
    for (size_t i = 0; i < urls.size(); ++i) {
        string filename = "page" + to_string(i + 1) + ".html";
        tasks.emplace(urls[i], filename);
    }

    vector<thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker, ref(tasks), urls.size());
    }

    for (auto& t : threads) {
        t.join();
    }
}

int main() {
    curl_global_init(CURL_GLOBAL_ALL);
    vector<string> urls = loadURLs("urls.txt");
    if (urls.empty()) {
        lock_guard<mutex> lock(mtx);
        cout << "No valid URLs found.\n";
        curl_global_cleanup();
        return 1;
    }

    downloadAll(urls);
    lock_guard<mutex> lock(mtx);
    cout << "Download complete! " << completed << " pages downloaded.\n";

    curl_global_cleanup();
    return 0;
}
