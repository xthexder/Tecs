
namespace example {
    class ComplexComponent {
    public:
        int Get() const {
            return value;
        }

        void Set(int newValue) {
            this->value = newValue;
            changed = true;
        }

        bool HasChanged() const {
            return changed;
        }

        void ResetChanged() {
            changed = false;
        }

    private:
        int value = 0;
        bool changed = false;
    };
} // namespace example
