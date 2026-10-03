#include "spellchecker.h"

#include <QWindow>

#import <AppKit/AppKit.h>

namespace {
constexpr int maxCachedLines = 2000;
}

SpellChecker::SpellChecker() {
    @autoreleasepool {
        m_tag = [NSSpellChecker uniqueSpellDocumentTag];
    }
}

SpellChecker::~SpellChecker() {
    @autoreleasepool {
        [[NSSpellChecker sharedSpellChecker] closeSpellDocumentWithTag:m_tag];
    }
}

bool SpellChecker::available() {
    return true;
}

bool SpellChecker::systemAutocorrectEnabled() {
    return [NSSpellChecker isAutomaticSpellingCorrectionEnabled];
}

// Lines are checked as the highlighter reaches them, so remember results per
// line text; editing a line only rechecks that one line.
QList<SpellChecker::Range> SpellChecker::misspellings(const QString &text) {
    const auto cached = m_cache.constFind(text);
    if (cached != m_cache.constEnd())
        return *cached;

    QList<Range> ranges;
    @autoreleasepool {
        NSSpellChecker *checker = [NSSpellChecker sharedSpellChecker];
        NSString *string = text.toNSString();
        NSInteger offset = 0;
        while (offset < NSInteger(string.length)) {
            const NSRange found = [checker checkSpellingOfString:string
                                                      startingAt:offset
                                                        language:nil
                                                            wrap:NO
                                          inSpellDocumentWithTag:m_tag
                                                       wordCount:nullptr];
            if (found.location == NSNotFound || found.length == 0)
                break;
            ranges.append({int(found.location), int(found.length)});
            offset = NSInteger(found.location + found.length);
        }
    }

    if (m_cache.size() > maxCachedLines)
        m_cache.clear();
    m_cache.insert(text, ranges);
    return ranges;
}

bool SpellChecker::isMisspelled(const QString &word) {
    const QList<Range> ranges = misspellings(word);
    return !ranges.isEmpty() && ranges.constFirst().start == 0
        && ranges.constFirst().length == word.length();
}

QStringList SpellChecker::guesses(const QString &word) {
    QStringList result;
    @autoreleasepool {
        NSString *string = word.toNSString();
        NSArray<NSString *> *found = [[NSSpellChecker sharedSpellChecker]
            guessesForWordRange:NSMakeRange(0, string.length)
                       inString:string
                       language:nil
         inSpellDocumentWithTag:m_tag];
        for (NSString *guess in found)
            result.append(QString::fromNSString(guess));
    }
    return result;
}

QString SpellChecker::correction(const QString &word) {
    @autoreleasepool {
        NSSpellChecker *checker = [NSSpellChecker sharedSpellChecker];
        NSString *string = word.toNSString();
        NSString *corrected = [checker correctionForWordRange:NSMakeRange(0, string.length)
                                                     inString:string
                                                     language:checker.language
                                       inSpellDocumentWithTag:m_tag];
        return corrected ? QString::fromNSString(corrected) : QString();
    }
}

// The shortcuts from System Settings > Keyboard > Text Replacements.
QString SpellChecker::replacement(const QString &word) const {
    @autoreleasepool {
        if (![NSSpellChecker isAutomaticTextReplacementEnabled])
            return {};
        NSString *with = [NSSpellChecker sharedSpellChecker]
                             .userReplacementsDictionary[word.toNSString()];
        return with ? QString::fromNSString(with) : QString();
    }
}

void SpellChecker::learn(const QString &word) {
    @autoreleasepool {
        [[NSSpellChecker sharedSpellChecker] learnWord:word.toNSString()];
    }
    m_cache.clear();
}

void SpellChecker::ignore(const QString &word) {
    @autoreleasepool {
        [[NSSpellChecker sharedSpellChecker] ignoreWord:word.toNSString()
                                 inSpellDocumentWithTag:m_tag];
    }
    m_cache.clear();
}

void SpellChecker::lookUp(QWindow *window, const QString &text, const QPointF &baseline,
                          qreal pointSize) {
    if (!window || text.trimmed().isEmpty())
        return;
    @autoreleasepool {
        NSView *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
        NSDictionary *attributes = @{NSFontAttributeName: [NSFont systemFontOfSize:pointSize]};
        NSAttributedString *string = [[NSAttributedString alloc] initWithString:text.toNSString()
                                                                     attributes:attributes];
        const CGFloat y = view.isFlipped ? baseline.y() : view.bounds.size.height - baseline.y();
        [view showDefinitionForAttributedString:string atPoint:NSMakePoint(baseline.x(), y)];
    }
}
