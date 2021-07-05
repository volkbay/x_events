//
// Created by Florian Mahlknecht on 2021-07-05.
// Copyright (c) 2021 NASA / JPL. All rights reserved.


#include <x/eklt/async_feature_tracker.h>
#include <x/vision/utils.h>
#include <x/vision/camera.h>
#include <easy/profiler.h>
#include <cassert>
#include <utility>
#include <iomanip>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/core/core.hpp>

using namespace x;


x::AsyncFeatureTracker::AsyncFeatureTracker(Camera camera, Params params, EventsPerformanceLoggerPtr event_perf_logger)
  : got_first_image_(false)
  , interpolator_(params, std::move(camera))
  , most_current_time_(-1.0)
  , params_(std::move(params))
  , event_perf_logger_(std::move(event_perf_logger)) {
}

void x::AsyncFeatureTracker::setParams(const Params &params) {
  params_ = params;
  interpolator_.setParams(params);

  if (params_.eklt_ekf_update_strategy == EkltEkfUpdateStrategy::EVERY_N_EVENTS) {
    events_till_next_ekf_update_ = params_.eklt_ekf_update_every_n;
  }
}

void x::AsyncFeatureTracker::extractFeatures(std::vector<cv::Point2d> &features, int num_patches, const ImageBuffer::iterator &image_it) {
  // mask areas which are within a distance min_distance of other features or along the border.
  int hp = (params_.eklt_patch_size - 1) / 2;

  int h = params_.img_height;
  int w = params_.img_width;
  cv::Mat mask = cv::Mat::ones(h, w, CV_8UC1);
  mask.rowRange(0, hp).colRange(0, w - 1).setTo(0);
  mask.rowRange(h - hp, h - 1).colRange(0, w - 1).setTo(0);
  mask.rowRange(0, h - 1).colRange(0, hp).setTo(0);
  mask.rowRange(0, h - 1).colRange(w - hp, w - 1).setTo(0);

  const int &min_distance = params_.eklt_min_distance;
  for (AsyncPatch* patch: getActivePatches()) {
    double min_x = std::fmax(patch->getCenter().x - min_distance, 0);
    double max_x = std::fmin(patch->getCenter().x + min_distance, w - 1);
    double min_y = std::fmax(patch->getCenter().y - min_distance, 0);
    double max_y = std::fmin(patch->getCenter().y + min_distance, h - 1);
    mask.rowRange(min_y, max_y).colRange(min_x, max_x).setTo(0);
  }

  // extract harris corners which are suitable
  // since they correspond to strong edges which also generate alot of events.
  VLOG(2) << "Harris corner detector with N=" << num_patches << " quality=" << params_.eklt_quality_level
          << " min_dist=" << params_.eklt_min_distance << " block_size=" << params_.eklt_block_size << " k=" << params_.eklt_k
          << " image_depth=" << image_it->second.depth() << " mask_ratio="
          << cv::sum(mask)[0] / (mask.cols * mask.rows);

  cv::goodFeaturesToTrack(image_it->second, features, num_patches,
                          params_.eklt_quality_level,
                          params_.eklt_min_distance, mask,
                          params_.eklt_block_size,
                          true,
                          params_.eklt_k);

  // initialize patches centered at the features with an initial pixel warp
  VLOG(1)
  << "Extracted " << features.size() << " new features on image at t=" << std::setprecision(15) << image_it->first
  << " s.";
}


class EventPerfHelper {
public:
  explicit EventPerfHelper(const EventsPerformanceLoggerPtr& ptr) {
    if (ptr) {
      perf_logger_ = ptr;
      t_start_ = profiler::now();
    }
  }

  ~EventPerfHelper() {
    if (perf_logger_) {
      perf_logger_->events_csv.addRow(t_start_, profiler::now());
    }
  }

  EventsPerformanceLoggerPtr perf_logger_;
  profiler::timestamp_t t_start_;
};

std::vector<x::MatchList> x::AsyncFeatureTracker::processEvents(const EventArray::ConstPtr &msg) {
  std::vector<MatchList> match_lists_for_ekf_updates;

  if (!got_first_image_) {
    LOG_EVERY_N(INFO, 20) << "Events dropped since no image present.";
    return match_lists_for_ekf_updates;
  }

  bool did_some_patch_change = false;
  for (const auto &ev : msg->events) {
    EASY_EVENT("Single Event");
    EventPerfHelper helper(event_perf_logger_);
    // keep track of the most current time with latest time stamp from event
    if (ev.ts >= most_current_time_)
      most_current_time_ = ev.ts;
    else if (fabs(ev.ts - most_current_time_) > 1e-6)  // if order wrong and spaced more than 1us
      LOG(WARNING) << "Processing event behind most current time: " << std::setprecision(15) << ev.ts << " < " << most_current_time_ << ". Events might not be in order!";

    // go through each patch and update the event frame with the new event
    for (AsyncPatch* patch: getActivePatches()) {
      did_some_patch_change |= updatePatch(*patch, ev);
    }

    if (updateFirstImageBeforeTime(most_current_time_, current_image_it_)) // enter if new image found
    {
      onNewImageReceived();

      // erase old image
      auto image_it = current_image_it_;
      image_it--;
      images_.erase(image_it);
    }

    switch (params_.eklt_ekf_update_strategy) {
      case EkltEkfUpdateStrategy::EVERY_ROS_EVENT_MESSAGE:
        // nothing to do here
        break;
      case EkltEkfUpdateStrategy::EVERY_N_EVENTS:
        if (--events_till_next_ekf_update_ <= 0) {
          if (did_some_patch_change) {
            events_till_next_ekf_update_ = params_.eklt_ekf_update_every_n;
            did_some_patch_change = false;
            match_lists_for_ekf_updates.push_back(interpolator_.getMatchListFromPatches(getActivePatches()));
          } else {
            events_till_next_ekf_update_ = 1; // try on next event again
          }
        }
        break;
      case EkltEkfUpdateStrategy::EVERY_N_MSEC_WITH_EVENTS:
        if (ev.ts - last_ekf_update_timestamp_ >= params_.eklt_ekf_update_every_n * 1e-3 && did_some_patch_change) {
          did_some_patch_change = false;
          last_ekf_update_timestamp_ = ev.ts;
          match_lists_for_ekf_updates.push_back(interpolator_.getMatchListFromPatches(getActivePatches()));
        }
        break;
    }

    onPostEvent();
  }

  if (did_some_patch_change && params_.eklt_ekf_update_strategy == EkltEkfUpdateStrategy::EVERY_ROS_EVENT_MESSAGE) {
    match_lists_for_ekf_updates.push_back(interpolator_.getMatchListFromPatches(getActivePatches()));
  }
  return match_lists_for_ekf_updates;
}

void x::AsyncFeatureTracker::processImage(double timestamp, const TiledImage &current_img) {
  // tiling not implemented
  assert(current_img.getNTilesH() == 1 && current_img.getNTilesW() == 1);
  images_.insert(std::make_pair(timestamp, current_img.clone()));

  if (!got_first_image_) {
    VLOG(1) << "Found first image.";
    current_image_it_ = images_.begin();
    most_current_time_ = current_image_it_->first;
    onInit(current_image_it_);
    got_first_image_ = true;
  }
}