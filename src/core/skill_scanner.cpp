#include "skill_scanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QRegularExpression>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// ============================================================
// SkillScanner
// ============================================================

SkillScanner::SkillScanner(QObject* parent)
    : QObject(parent)
{}

// ---------- 扫描一个根目录 ----------
QList<Skill> SkillScanner::scanRoot(const QString& rootPath, Repo* repo) {
    QList<Skill> result;
    QSet<QString> seenPaths; // 路径去重

    QStringList mdList = collectSkillMdPaths(rootPath);
    int total = mdList.size();

    for (int i = 0; i < total; ++i) {
        const QString& p = mdList.at(i);
        emit progress(i + 1, total, p);

        QString absPath = QFileInfo(p).absoluteFilePath();
        if (seenPaths.contains(absPath)) continue;
        seenPaths.insert(absPath);

        Skill sk = parseSkillMd(p, repo);
        if (!sk.name.isEmpty()) {
            result.append(sk);
        }
    }
    return result;
}

// ---------- 扫描所有 ----------
QList<Skill> SkillScanner::scanAll(const QString& doubaoSkillsRoot,
                                   const QList<Repo>& existingRepos) {
    QList<Skill> result;
    QSet<QString> seenPaths;
    QDir rootDir(doubaoSkillsRoot);
    if (!rootDir.exists()) return result;

    // 把 Repo.localPath 映射到 Repo* 方便查找
    QMap<QString, Repo> repoByPath;
    for (const Repo& r : existingRepos) {
        repoByPath.insert(QFileInfo(r.localPath).absoluteFilePath(), r);
    }

    // 先把 root 自己也扫一下（root 下可能直接有 SKILL.md）
    auto processOne = [&](const QString& dirPath, Repo* repo) {
        QStringList mdList = collectSkillMdPaths(dirPath);
        for (const QString& p : mdList) {
            QString absPath = QFileInfo(p).absoluteFilePath();
            if (seenPaths.contains(absPath)) continue;
            seenPaths.insert(absPath);

            Skill sk = parseSkillMd(p, repo);
            if (sk.name.isEmpty()) continue;

            // 标记 source
            if (repo != nullptr) {
                // repo 根目录：SKILL.md 相对于 repo 根 = 根下直接有 → git-root；否则 git-sub
                QString repoRoot = QFileInfo(repo->localPath).absoluteFilePath();
                QString rel = QFileInfo(p).absoluteFilePath();
                rel.remove(repoRoot);
                rel.remove('/');
                rel.remove('\\');
                if (rel == "SKILL.md") {
                    sk.source = "git-root";
                } else {
                    sk.source = "git-sub";
                    // 推断 source-subpath
                    QString relDir = QFileInfo(p).absolutePath();
                    relDir.remove(repoRoot);
                    relDir.remove(QRegularExpression("^[/\\\\]+"));
                    sk.sourceSubpath = relDir;
                }
                sk.repoId = repo->id;
            } else {
                // 本地
                if (QDir(sk.directory).exists(".git")) {
                    sk.source = "git-root";
                } else {
                    sk.source = "local";
                }
            }
            result.append(sk);
        }
    };

    // 处理 root 自己
    processOne(doubaoSkillsRoot, nullptr);

    // 遍历一级子目录
    QStringList subDirs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& sub : subDirs) {
        QString subPath = rootDir.filePath(sub);
        QDir subDir(subPath);

        bool hasGit = subDir.exists(".git");
        auto it = repoByPath.find(QFileInfo(subPath).absoluteFilePath());
        Repo matchedRepo;
        Repo* repoPtr = nullptr;

        if (it != repoByPath.end()) {
            matchedRepo = it.value();
            repoPtr = &matchedRepo;
        } else if (hasGit) {
            // 没匹配到但有 .git → 构造一个临时 Repo
            matchedRepo.name = sub;
            matchedRepo.localPath = subPath;
            repoPtr = &matchedRepo;
        }

        if (repoPtr != nullptr) {
            processOne(subPath, repoPtr);
        } else {
            // 纯本地：只扫这个子目录（不再递归下钻一级之外的 SKILL.md）
            processOne(subPath, nullptr);
        }
    }

    return result;
}

// ---------- 递归找所有 SKILL.md ----------
QStringList SkillScanner::collectSkillMdPaths(const QString& rootPath) {
    QStringList result;
    QDir dir(rootPath);
    if (!dir.exists()) return result;

    QStringList patterns = {
        "SKILL.md",
        "skills/*/SKILL.md",
        ".github/skills/*/SKILL.md",
        ".claude/skills/*/SKILL.md",
        ".agents/skills/*/SKILL.md",
        ".cursor/skills/*/SKILL.md",
        "plugins/*/skills/*/SKILL.md"
    };

    for (const QString& pat : patterns) {
        QDirIterator it(rootPath, QStringList{ pat },
                        QDir::Files | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            result.append(it.next());
        }
    }

    // 去重
    result.removeDuplicates();
    return result;
}

// ---------- 解析 SKILL.md ----------
Skill SkillScanner::parseSkillMd(const QString& skillMdPath, Repo* repo) {
    Skill s;
    s.skillMdPath = QFileInfo(skillMdPath).absoluteFilePath();
    s.directory   = QFileInfo(skillMdPath).absolutePath();

    QFile f(skillMdPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "parseSkillMd cannot open" << skillMdPath;
        return s;
    }
    QByteArray raw = f.readAll();
    QString content = QString::fromUtf8(raw);

    Frontmatter fm = parseFrontmatter(content);

    // name
    QString rawName = fm.scalarFields.value("name");
    if (rawName.isEmpty()) rawName = fm.scalarFields.value("title");
    if (rawName.isEmpty()) rawName = QFileInfo(s.directory).fileName();
    if (rawName.isEmpty()) return s; // 没有 name 也没有目录名 → 丢弃

    s.name = rawName;
    s.cleanName = cleanName(rawName);
    s.canonicalName = canonicalName(s.cleanName);

    // description
    QString desc = fm.scalarFields.value("description");
    if (desc.isEmpty()) desc = fm.scalarFields.value("summary");
    s.description = desc;
    s.descriptionZh = desc; // 简单处理：description 和 descriptionZh 共用

    // body（frontmatter 之后的正文）
    s.body = fm.body;

    // version
    // （暂无直接字段映射，保留空）

    // source 相关：repo 处理在 scanAll / scanRoot 里赋值，这里保持默认
    if (repo != nullptr) {
        s.repoId = repo->id;
    }

    // standard 推断
    bool hasAllowedToolsOrCompat = fm.scalarFields.contains("allowed-tools")
                                || fm.scalarFields.contains("allowed_tools")
                                || fm.scalarFields.contains("compatibility");
    s.standard = inferStandard(s.skillMdPath, s.description, hasAllowedToolsOrCompat);

    // metadata（JSON 字符串存 tags）
    QString metadataJson = fm.scalarFields.value("metadata");
    if (!metadataJson.isEmpty()) {
        // 尝试解析确认是合法 JSON
        QJsonParseError err;
        QJsonDocument::fromJson(metadataJson.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError) {
            // 不是 JSON → 丢进 tags 原封不动
            if (!s.tags.isEmpty()) s.tags += ",";
            s.tags += metadataJson;
        }
    }

    // supported_os → 也塞进 tags
    QStringList osList;
    if (fm.listFields.contains("supported_os")) osList = fm.listFields.value("supported_os");
    if (fm.listFields.contains("supported-os")) osList = fm.listFields.value("supported-os");
    if (!osList.isEmpty()) {
        QString osTag = osList.join(",");
        if (!s.tags.isEmpty()) s.tags += ",";
        s.tags += osTag;
    }

    // ---------- 附属目录检测 ----------
    QDir skillDir(s.directory);
    s.hasChapters   = skillDir.exists("chapters")   && QDir(skillDir.filePath("chapters")).entryList(QDir::Files).size() > 0;
    s.hasPatterns   = skillDir.exists("patterns")   && QDir(skillDir.filePath("patterns")).entryList(QDir::Files).size() > 0;
    s.hasGlossary   = skillDir.exists("glossary.md")   || skillDir.exists("Glossary.md");
    s.hasCheatsheet = skillDir.exists("cheatsheet.md") || skillDir.exists("Cheatsheet.md");

    // ---------- 文件统计 ----------
    qint64 totalSize = 0;
    int fileCount = 0;
    QDirIterator dIt(s.directory, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (dIt.hasNext()) {
        dIt.next();
        fileCount++;
        totalSize += dIt.fileInfo().size();
    }
    s.totalFiles = fileCount;
    s.fileSize   = totalSize;
    s.fileCount  = fileCount;
    s.mtime      = QFileInfo(s.directory).lastModified().toSecsSinceEpoch();

    // ---------- is_new ----------
    s.isNew = containsNewMark(rawName) || containsNewMark(QFileInfo(s.directory).fileName());

    // ---------- createdAt / updatedAt ----------
    s.createdAt = QDateTime::currentSecsSinceEpoch();
    s.updatedAt = s.createdAt;

    return s;
}

// ============================================================
// Frontmatter 解析（简化版 YAML 解析，满足 SKILL.md 常用格式）
// ============================================================

SkillScanner::Frontmatter SkillScanner::parseFrontmatter(const QString& mdContent) {
    Frontmatter fm;
    fm.body = mdContent;

    // 必须以 --- 开头
    if (!mdContent.startsWith("---") && !mdContent.startsWith("\n---")) {
        return fm;
    }

    QString trimmed = mdContent;
    // 去掉开头 ---
    int idx = trimmed.indexOf(QRegularExpression("^---[\\r\\n]+"));
    if (idx < 0) return fm;
    int start = trimmed.indexOf('\n', idx);
    if (start < 0) return fm;
    QString rest = trimmed.mid(start + 1);

    // 找第二个 ---
    int endIdx = rest.indexOf(QRegularExpression("\n---[\\r\\n]"));
    if (endIdx < 0) {
        // 也可能结尾直接是 ---（没 \n 后缀）
        endIdx = rest.indexOf(QRegularExpression("\n---$"));
    }
    if (endIdx < 0) return fm;
    QString fmText = rest.left(endIdx);
    fm.body = rest.mid(endIdx + 4); // 跳过 \n---

    // 逐行解析
    QStringList lines = fmText.split(QRegularExpression("\\r?\\n"));
    QString currentKey;
    bool inList = false;

    for (const QString& rawLine : lines) {
        QString line = rawLine;
        if (line.isEmpty()) continue;

        // 列表项：   - value
        if (inList) {
            QRegularExpression re("^\\s*-\\s*(.*)$");
            auto m = re.match(line);
            if (m.hasMatch()) {
                QString val = m.captured(1).trimmed();
                if (val.startsWith('"') && val.endsWith('"')) val = val.mid(1, val.size() - 2);
                if (val.startsWith('\'') && val.endsWith('\'')) val = val.mid(1, val.size() - 2);
                fm.listFields[currentKey].append(val);
                continue;
            }
            inList = false;
        }

        // scalar 行:  key: value
        int colon = line.indexOf(':');
        if (colon <= 0) continue;

        QString key = line.left(colon).trimmed();
        QString val = line.mid(colon + 1).trimmed();

        if (val.isEmpty()) {
            currentKey = key;
            inList = false;
            continue;
        }

        QString cleaned = val;
        if (cleaned.startsWith('"') && cleaned.endsWith('"')) cleaned = cleaned.mid(1, cleaned.size() - 2);
        if (cleaned.startsWith('\'') && cleaned.endsWith('\'')) cleaned = cleaned.mid(1, cleaned.size() - 2);

        // 如果当前 key 已经存过 scalar，只有当新 key 是缩进续行（冒号位置在前一行 key 的冒号之后）
        // 才拼上。否则是新 key，覆盖 currentKey。
        bool isContinuation = !currentKey.isEmpty() && fm.scalarFields.contains(currentKey);
        if (isContinuation) {
            // 看当前行是否以空格/Tab 开头（缩进行）
            QRegularExpression indentRe("^\\s+");
            if (indentRe.match(rawLine).hasMatch()) {
                QString& existing = fm.scalarFields[currentKey];
                existing += " " + cleaned;
                continue;
            }
            // 不是缩进 → 新 key，continue 到下面
        }

        fm.scalarFields[key] = cleaned;
        currentKey = key;
        inList = false;
    }

    return fm;
}

// ============================================================
// clean_name / canonical_name / is_new
// ============================================================

QString SkillScanner::cleanName(const QString& rawName) {
    QString s = rawName;

    // 去 (NEW) / （NEW） 前缀
    s.remove(QRegularExpression("^\\s*\\(NEW\\)\\s*", QRegularExpression::CaseInsensitiveOption));
    s.remove(QRegularExpression("^\\s*（NEW）\\s*"));

    // 去方括号 [] 及其内容
    s.remove(QRegularExpression("\\[[^\\]]*\\]"));
    // 去尖括号 《》
    s.remove(QRegularExpression("《[^》]*》"));

    // 去多余空白
    s = s.trimmed();
    s.replace(QRegularExpression("\\s+"), " ");
    return s;
}

QString SkillScanner::canonicalName(const QString& cleanNameIn) {
    QString s = cleanNameIn.toLower();

    // 替换非 a-z0-9 的字符为连字符
    QString out;
    bool prevDash = false;
    for (QChar c : s) {
        if (c.isLetterOrNumber()) {
            // 如果是中文或非 ASCII → 跳过（或用 hash 替代）
            if (c.toLatin1() == 0) {
                prevDash = true;
                continue;
            }
            out.append(c);
            prevDash = false;
        } else if (c == '-' || c == '_' || c.isSpace()) {
            if (!prevDash) { out.append('-'); prevDash = true; }
        } else {
            prevDash = true;
        }
    }

    // 如果全是空（比如全中文）→ 用 hash
    if (out.isEmpty()) {
        qsizetype h = qHash(cleanNameIn);
        return QString("skill-%1").arg(h & 0x7FFFFFFF, 0, 16);
    }

    // 去首尾 -
    while (out.startsWith('-')) out.remove(0, 1);
    while (out.endsWith('-'))   out.chop(1);

    // 合并连续 ---
    out.replace(QRegularExpression("-+"), "-");

    // 截断 64 字符
    if (out.size() > 64) out.truncate(64);

    // 再检查首尾
    while (out.startsWith('-')) out.remove(0, 1);
    while (out.endsWith('-'))   out.chop(1);

    return out;
}

bool SkillScanner::containsNewMark(const QString& s) {
    return s.contains("(NEW)", Qt::CaseInsensitive)
        || s.contains("（NEW）");
}

// ============================================================
// standard 推断
// ============================================================

QString SkillScanner::inferStandard(const QString& skillMdPath,
                                    const QString& description,
                                    bool hasAllowedToolsOrCompat) {
    // 路径优先
    if (skillMdPath.contains("/.github/skills/") || skillMdPath.contains("\\.github\\skills\\")) {
        return "github-copilot";
    }
    if (skillMdPath.contains("/.claude/skills/") || skillMdPath.contains("\\.claude\\skills\\")) {
        return "claude";
    }
    if (skillMdPath.contains("/.agents/skills/") || skillMdPath.contains("\\.agents\\skills\\")) {
        return "agent-skills"; // generic-agent
    }
    if (skillMdPath.contains("/.cursor/skills/") || skillMdPath.contains("\\.cursor\\skills\\")) {
        return "cursor";
    }
    if (hasAllowedToolsOrCompat) return "agent-skills";

    // 中文检测
    bool hasChinese = false;
    for (QChar c : description) {
        if (c.unicode() >= 0x4E00 && c.unicode() <= 0x9FFF) {
            hasChinese = true; break;
        }
    }
    if (hasChinese) return "doubao";

    return "mixed";
}
