#pragma once
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace vision {
// Single writer, complete records only. Owns only files created in this run.
// Zero chunkBytes preserves the complete offline frames.jsonl contract.
class FrameLog {
 public:
  struct Part { std::string name; std::uint64_t records=0; };
  FrameLog(std::filesystem::path directory,std::size_t chunkBytes=0,std::size_t keepFiles=4)
      : directory_(std::move(directory)),limit_(chunkBytes),keep_(keepFiles) {
    if(!keep_) throw std::invalid_argument("Frame log retention must be positive");
    stream_.exceptions(std::ios::badbit|std::ios::failbit);
    openPart();
  }
  void append(const std::string& record) {
    if(closed_) throw std::logic_error("Frame log already closed");
    if(record.empty() || record.find('\n')!=record.size()-1)
      throw std::invalid_argument("Frame record must contain exactly one trailing newline");
    if(limit_ && record.size()>limit_) throw std::length_error("Frame record exceeds log chunk budget");
    if(limit_ && bytes_+record.size()>limit_) {
      stream_.close();
      if(parts_.size()==keep_) {
        if(!std::filesystem::remove(directory_/parts_.front().name))
          throw std::runtime_error("Cannot retire owned frame log segment");
        retired_+=parts_.front().records;parts_.pop_front();
      }
      openPart();
    }
    stream_.write(record.data(),static_cast<std::streamsize>(record.size()));
    bytes_+=record.size();++total_;++parts_.back().records;
  }
  void close() { if(!closed_) { stream_.close();closed_=true; } }
  std::uint64_t total() const { return total_; }
  std::uint64_t retired() const { return retired_; }
  const std::deque<Part>& parts() const { return parts_; }
 private:
  void openPart() {
    const auto name=limit_ ? "frames-"+std::to_string(sequence_++)+".jsonl" : "frames.jsonl";
    const auto path=directory_/name;
    if(std::filesystem::exists(path)) throw std::runtime_error("Refusing to overwrite existing frame log");
    stream_.open(path,std::ios::out|std::ios::binary);
    parts_.push_back({name,0});bytes_=0;
  }
  std::filesystem::path directory_;
  std::ofstream stream_;
  std::deque<Part> parts_;
  std::size_t limit_,keep_,bytes_=0;
  std::uint64_t sequence_=0,total_=0,retired_=0;
  bool closed_=false;
};
}
