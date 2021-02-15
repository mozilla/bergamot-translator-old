//
// Defines:
//
// Request: holds the input blob of a text, Segments (vector<Words>) which are
// to go to the batching mechanism and alignments between the processed
// segments and the input blob (sourceTokenRanges). In addition, Request takes
// care of the barrier which fires when all the Segments in a request are done
// translating by the workers (BatchTranslator).
// TODO(jerinphilip):  Extend Request with notions of Priority (sequence,
// user-given).
//
// RequestSentence: is a tuple of (index, Ptr<Request>). This provides the
// batching mechanism access to the segment within the request. The backref to
// Request allows event triggering the barrier upon completion of the last
// sentence by a worker.
//
// Batch: is a vector of RequestSentences tagged with a batchNumber, which is
// what the PCQueue holds. Batch is "produced" by the Batcher.

#ifndef SRC_BERGAMOT_REQUEST_H_
#define SRC_BERGAMOT_REQUEST_H_

#include "definitions.h"
#include "response.h"

#include "common/logging.h"
#include "data/types.h"
#include "translator/beam_search.h"

#include <cassert>

#include <future>
#include <vector>

namespace marian {
namespace bergamot {

class Request {
public:
  Request(unsigned int Id, int lineNumberBegin,
          std::vector<Ptr<Vocab const>> &vocabs_, std::string &&source,
          Segments &&segments, std::vector<TokenRanges> &&sourceTokenRanges,
          std::promise<Response> responsePromise);

  // Obtain the count of tokens in the segment correponding to index. Used to
  // insert sentence from multiple requests into the corresponding size bucket.
  size_t segmentTokens(size_t index) const;

  // Obtain number of segments in a request.
  size_t numSegments() const;
  size_t lineNumberBegin() const;

  // Obtains segment corresponding to index  to create a batch of segments among
  // several requests.
  Segment getSegment(size_t index) const;

  // For notions of priority among requests, used to enable std::set in
  // Batcher.
  bool operator<(const Request &request) const;

  // Processes a history obtained after translating in a heterogenous batch
  // compiled from requests.
  void processHistory(size_t index, Ptr<History> history);

  // On completion of last segment, sets value of the promise.
  void completeRequest();

private:
  unsigned int Id_;
  int lineNumberBegin_;

  // Multiple translation-workers can concurrently access the same Request. The
  // following atomic atomically operates on the variable holding sentences
  // remaining to be translated.
  std::atomic<int> counter_;

  // source_ holds the source string to be translated. segments_ hold the
  // sentences generated from source_ in vector<Words>. sourceTokenRanges_ are
  // string_views of the text corresponding to these words, pointing to
  // sequences in source_. histories_ is a buffer which eventually stores the
  // translations of each segment in the corresponding index.
  std::string source_;
  Segments segments_;
  std::vector<TokenRanges> sourceTokenRanges_;
  std::vector<Ptr<History>> histories_;

  // Members above are moved into newly constructed Response on completion
  // of translation of all segments. The promise below is set to this Response
  // value. future to this promise is made available to the user through
  // Service.
  std::promise<Response> response_;

  // Constructing Response requires the vocabs_ used to generate Request.
  std::vector<Ptr<Vocab const>> *vocabs_;
};

class RequestSentence {
  // A RequestSentence provides a view to a sentence within a Request. Existence
  // of this class allows the sentences and associated information to be kept
  // within Request.

public:
  RequestSentence(size_t, Ptr<Request>);
  size_t numTokens() const;

  // lineNumber in Request, used for matching marian-decoder. SentenceTuple
  // requires lineNumber to be set for Corpus based batches.
  size_t lineNumber() const;

  // Accessor to the segment represented by the RequestSentence.
  Segment getUnderlyingSegment() const;

  // Forwards call to Request, checking for completion.
  void completeSentence(Ptr<History> history);

  friend bool operator<(const RequestSentence &a, const RequestSentence &b);

private:
  size_t index_;
  Ptr<Request> request_;
};

typedef std::vector<RequestSentence> RequestSentences;

class Batch {
public:
  Batch() { reset(); }
  // Reset is required to reuse the same batch by consumer.
  void reset();

  //  Methods to construct and determine poison.
  static Batch poison() {
    Batch poison_;
    poison_.Id_ = -1;
    return poison_;
  }
  bool isPoison() const { return (Id_ == -1); }

  size_t size() const { return sentences_.size(); }

  // Accessors to load data into a batch. Use add(...) to add sentences into a
  // batch. Once complete with a legal batch, use setId to set Id_ accordingly.
  // setId only allows setting Id > 0. For use in Batcher, which acts as a
  // producer to a PCQueue holding "Batch"es.
  //
  // Id_ =
  //    -1 : Batch::Poison
  //     0 : Empty Batch
  //    >0 : Legal batch containing sentences

  void add(const RequestSentence &sentence);
  void setId(int Id);

  // Accessors to read from a Batch. For use in BatchTranslator (consumer on a
  // PCQueue holding batches).
  //
  // sentences() are used to access sentences to construct marian internal
  // batch.
  const RequestSentences &sentences() { return sentences_; }

  // On obtaining Histories after translating a batch, completeBatch can be
  // called with Histories , which forwards the call to Request through
  // RequestSentence and triggers completion, by setting the promised value to
  // the future given to client.
  void completeBatch(const Histories &histories);

  // Convenience function to log batch-statistics. numTokens, max-length.
  // TODO(jerinphilip): Use to log and report packing efficiency.
  void log();

private:
  int Id_;
  RequestSentences sentences_;
};

} // namespace bergamot
} // namespace marian

#endif // SRC_BERGAMOT_REQUEST_H_
