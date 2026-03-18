FROM python:3.12-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY app ./app
COPY .env.example ./.env.example

ENV SAVE_ROOT=/data/saves
ENV HISTORY_ROOT=/data/history
ENV INDEX_PATH=/data/index.json
ENV ENABLE_VERSION_HISTORY=true

EXPOSE 8080

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8080"]
