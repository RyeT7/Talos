FROM python:3.13-slim

WORKDIR /app

COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY service.py temp_forecast_model.pkl anomaly_model.pkl ac_recommender_model.pkl ./

USER nobody

ENTRYPOINT [ "python", "-u", "service.py" ]
