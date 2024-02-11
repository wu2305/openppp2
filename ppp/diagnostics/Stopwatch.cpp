#include <ppp/diagnostics/Stopwatch.h>

#include <iostream>
#include <ctime>
#include <chrono>

// ������į�� �¹�]���f
// �P�L��ȥ�Ĵ���
// �Ҹ��팢�ۺ�����ٴ���
// ��ס ���Д��ğ��n
// �����@һ�E ���B�����^
// һ����Ĳ���
// ���D����Ư���еĶɿ�
// ��ס �ܳ�������
// �Ў׷� ڤڤ֮�о���
// ���ڵ� �]�ȵ��Ļ��^
// �����������p��
// �sƫ�H������
// ����߀���x��
// �Ў׷� ����֮���և
// ����� һ�������
// �҂��]�ߵ�����
// �Y���Ƶ��_�^
// ���o̎��׷��

namespace ppp 
{
    namespace diagnostics
    {
        template <typename Duration>
        static constexpr int64_t ElapsedTimed(std::chrono::high_resolution_clock::time_point start, std::chrono::high_resolution_clock::time_point stop) noexcept
        {
            if (stop == std::chrono::high_resolution_clock::time_point())
            {
                stop = std::chrono::high_resolution_clock::now();
            }

            return std::chrono::duration_cast<Duration>(stop - start).count();
        }

        void Stopwatch::Start() noexcept
        {
            std::chrono::high_resolution_clock::time_point null_;
            do
            {
                SynchronizeObjectScope scope(syncobj_);
                stop_ = null_;
                if (start_ == null_)
                {
                    start_ = std::chrono::high_resolution_clock::now();
                }
            } while (false);
        }

        void Stopwatch::Stop() noexcept
        {
            std::chrono::high_resolution_clock::time_point null_;
            do
            {
                SynchronizeObjectScope scope(syncobj_);
                if (start_ == null_)
                {
                    start_ = null_;
                    stop_ = null_;
                }
                else
                {
                    stop_ = std::chrono::high_resolution_clock::now();
                }
            } while (false);
        }

        void Stopwatch::Reset() noexcept
        {
            std::chrono::high_resolution_clock::time_point null_;
            do
            {
                SynchronizeObjectScope scope(syncobj_);
                start_ = null_;
                stop_ = null_;
            } while (false);
        }

        void Stopwatch::Restart() noexcept
        {
            SynchronizeObjectScope scope(syncobj_);
            start_ = std::chrono::high_resolution_clock::now();
            stop_ = std::chrono::high_resolution_clock::time_point();
        }

        int64_t Stopwatch::ElapsedMilliseconds() noexcept
        {
            SynchronizeObjectScope scope(syncobj_);
            return ElapsedTimed<std::chrono::milliseconds>(start_, stop_);
        }

        int64_t Stopwatch::ElapsedTicks() noexcept
        {
            SynchronizeObjectScope scope(syncobj_);
            return ElapsedTimed<std::chrono::nanoseconds>(start_, stop_);
        }

        bool Stopwatch::IsRunning() noexcept
        {
            std::chrono::high_resolution_clock::time_point null_;
            do
            {
                SynchronizeObjectScope scope(syncobj_);
                return start_ != null_ && stop_ == null_;
            } while (false);
        }

        DateTime Stopwatch::Elapsed() noexcept
        {
            int64_t ms = ElapsedMilliseconds();
            return DateTime::MinValue().AddMilliseconds(ms);
        }
    }
}