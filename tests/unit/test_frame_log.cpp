#include <gtest/gtest.h>
#include <chrono>
#include <iterator>
#include "vision/core/FrameLog.hpp"
#ifdef VISION_WITH_OPENCV
#include "../../apps/trt_infer/replay_config.hpp"
#endif

namespace {
struct TemporaryLogDirectory {
  std::filesystem::path path=std::filesystem::current_path()/
      ("vision-log-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryLogDirectory() { if(!std::filesystem::create_directory(path)) throw std::runtime_error("Test directory collision"); }
  ~TemporaryLogDirectory() { std::error_code error;std::filesystem::remove_all(path,error); }
};
std::string read(const std::filesystem::path& path) {
  std::ifstream file(path,std::ios::binary);
  return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
}

TEST(FrameLog, OfflineKeepsEveryRecordInOriginalFile) {
  TemporaryLogDirectory d;
  vision::FrameLog log(d.path);
  log.append("{\"frame\":0}\n");log.append("{\"frame\":1}\n");log.close();
  EXPECT_EQ(read(d.path/"frames.jsonl"),"{\"frame\":0}\n{\"frame\":1}\n");
  EXPECT_EQ(log.total(),2u);EXPECT_EQ(log.retired(),0u);
  EXPECT_THROW(log.append("{}\n"),std::logic_error);
}

TEST(FrameLog, RotationKeepsCompleteNewestRecordsWithinBudget) {
  TemporaryLogDirectory d;
  std::ofstream(d.path/"unrelated.txt")<<"preserve";
  vision::FrameLog log(d.path,4,2);
  for(int i=0;i<10;++i) log.append(std::to_string(i)+"\n");
  log.close();
  ASSERT_EQ(log.parts().size(),2u);
  EXPECT_EQ(log.total(),10u);EXPECT_EQ(log.retired(),6u);
  EXPECT_EQ(read(d.path/log.parts()[0].name),"6\n7\n");
  EXPECT_EQ(read(d.path/log.parts()[1].name),"8\n9\n");
  EXPECT_EQ(read(d.path/"unrelated.txt"),"preserve");
  std::uintmax_t bytes=0;int files=0;
  for(const auto& file:std::filesystem::directory_iterator(d.path)) if(file.path().extension()==".jsonl") {
    bytes+=file.file_size();++files;
  }
  EXPECT_LE(bytes,8u);EXPECT_EQ(files,2);
}

TEST(FrameLog, OversizedOrIncompleteRecordDoesNotEraseEvidence) {
  TemporaryLogDirectory d;vision::FrameLog log(d.path,4,1);
  log.append("ok\n");
  EXPECT_THROW(log.append("oversized\n"),std::length_error);
  EXPECT_THROW(log.append("bad"),std::invalid_argument);
  EXPECT_THROW(log.append("a\nb\n"),std::invalid_argument);
  log.close();EXPECT_EQ(log.total(),1u);EXPECT_EQ(read(d.path/log.parts()[0].name),"ok\n");
}

TEST(FrameLog, ExistingFileIsNeverOverwritten) {
  TemporaryLogDirectory d;std::ofstream(d.path/"frames.jsonl")<<"original";
  EXPECT_THROW(vision::FrameLog log(d.path),std::runtime_error);
  EXPECT_EQ(read(d.path/"frames.jsonl"),"original");
  EXPECT_THROW(vision::FrameLog log(d.path,4,0),std::invalid_argument);
}

#ifdef VISION_WITH_OPENCV
TEST(FrameLog, ReplayConfigurationDefaultsAndValidation) {
  TemporaryLogDirectory d;
  const auto path=d.path/"config.yml";
  const std::string base="%YAML:1.0\nqueue_capacity: 1\nqueue_mode: latest\nrealtime_pace: 1\n";
  {std::ofstream(path)<<base;}
  auto c=replay::ReplayConfig::load(path.string());
  EXPECT_EQ(c.log_chunk_bytes,0);EXPECT_TRUE(c.write_overlay);
  for(const std::string invalid:{"log_chunk_bytes: -1\n","log_chunk_bytes: 2.5\n","log_keep_files: 0\n","write_overlay: 2\n"}) {
    {std::ofstream(path)<<base<<invalid;}
    EXPECT_THROW(replay::ReplayConfig::load(path.string()),std::runtime_error);
  }
  {std::ofstream(path)<<base<<"log_chunk_bytes: 1024\nlog_keep_files: 2\nwrite_overlay: 0\n";}
  c=replay::ReplayConfig::load(path.string());
  EXPECT_EQ(c.log_chunk_bytes,1024);EXPECT_EQ(c.log_keep_files,2);EXPECT_FALSE(c.write_overlay);
}
#endif
